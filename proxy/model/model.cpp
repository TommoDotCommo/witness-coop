// See model.h. Mutants (-DWM_MUT_...) each break one thing on purpose; the T0 test must fail for every one of them.
#include "model.h"
#include "gestures.h"
#include "life.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include "../third_party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../third_party/stb_image.h"

namespace wm {

// ---- small matrix helpers
static void Identity(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void Mul(const float* a, const float* b, float* out) {      // out = a * b
    float r[16];
    for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + row] * b[c * 4 + k];
        r[c * 4 + row] = s;
    }
    memcpy(out, r, 64);
}
static void FromTRS(const float* t, const float* q, const float* s, float* m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0];     m[2] = (2 * (x * z - y * w)) * s[0];     m[3] = 0;
    m[4] = (2 * (x * y - z * w)) * s[1];     m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1];     m[7] = 0;
    m[8] = (2 * (x * z + y * w)) * s[2];     m[9] = (2 * (y * z - x * w)) * s[2];     m[10] = (1 - 2 * (x * x + y * y)) * s[2]; m[11] = 0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
}
static void Decompose(const float* m, float* t, float* q, float* s) {      // no shear assumed
    t[0] = m[12]; t[1] = m[13]; t[2] = m[14];
    for (int c = 0; c < 3; c++) s[c] = sqrtf(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
    float r[9]; for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) r[c * 3 + k] = s[c] > 1e-12f ? m[c * 4 + k] / s[c] : (c == k);
    // r[c*3+row]; rotation matrix entries R(row, col) = r[col*3+row]
    float r00 = r[0], r10 = r[1], r20 = r[2], r01 = r[3], r11 = r[4], r21 = r[5], r02 = r[6], r12 = r[7], r22 = r[8];
    float tr = r00 + r11 + r22;
    if (tr > 0) { float S = sqrtf(tr + 1) * 2; q[3] = 0.25f * S; q[0] = (r21 - r12) / S; q[1] = (r02 - r20) / S; q[2] = (r10 - r01) / S; }
    else if (r00 > r11 && r00 > r22) { float S = sqrtf(1 + r00 - r11 - r22) * 2; q[3] = (r21 - r12) / S; q[0] = 0.25f * S; q[1] = (r01 + r10) / S; q[2] = (r02 + r20) / S; }
    else if (r11 > r22) { float S = sqrtf(1 + r11 - r00 - r22) * 2; q[3] = (r02 - r20) / S; q[0] = (r01 + r10) / S; q[1] = 0.25f * S; q[2] = (r12 + r21) / S; }
    else { float S = sqrtf(1 + r22 - r00 - r11) * 2; q[3] = (r10 - r01) / S; q[0] = (r02 + r20) / S; q[1] = (r12 + r21) / S; q[2] = 0.25f * S; }
}
void Slerp(const float* a, const float* b, float t, float* out) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], bb[4] = {b[0], b[1], b[2], b[3]};
    if (d < 0) { d = -d; for (float& v : bb) v = -v; }
    float ka, kb;
#ifdef WM_MUT_NO_NORMALIZE
    if (true) { ka = 1 - t; kb = t; }      // plain lerp without renormalising: shrinks the rotation half way
#else
    if (d > 0.9995f) { ka = 1 - t; kb = t; }
#endif
    else { float th = acosf(d), sn = sinf(th); ka = sinf((1 - t) * th) / sn; kb = sinf(t * th) / sn; }
    float q[4]; for (int i = 0; i < 4; i++) q[i] = ka * a[i] + kb * bb[i];
#ifndef WM_MUT_NO_NORMALIZE
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); if (l > 0) for (float& v : q) v /= l;
#endif
    memcpy(out, q, 16);
}

// ---- loading
bool LoadGlb(const uint8_t* data, size_t n, Model* out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    cgltf_options opt = {}; cgltf_data* d = nullptr;
    if (cgltf_parse(&opt, data, n, &d) != cgltf_result_success) return fail("not a glTF file");
    struct Free { cgltf_data* d; ~Free() { cgltf_free(d); } } guard{d};
    if (cgltf_load_buffers(&opt, d, nullptr) != cgltf_result_success) return fail("cannot load buffers (use a .glb with embedded data)");
    Model m;
    std::vector<const cgltf_node*> order; order.reserve(d->nodes_count);
    for (size_t i = 0; i < d->nodes_count; i++) {
        const cgltf_node* nd = &d->nodes[i]; Node o; o.parent = nd->parent ? (int)(nd->parent - d->nodes) : -1; if (nd->name) o.name = nd->name;
        float loc[16]; cgltf_node_transform_local(nd, loc);
        Decompose(loc, o.t, o.r, o.s);
        m.nodes.push_back(o);
    }
    // Every skin of the file feeds one joint list (a model may have one skin per mesh, all on the same skeleton): a joint is a node, taken once, with the first inverse bind matrix seen.
    std::vector<std::vector<int>> skinJoint(d->skins_count);
    for (size_t si = 0; si < d->skins_count; si++) {
        const cgltf_skin* sk = &d->skins[si];
        for (size_t j = 0; j < sk->joints_count; j++) {
            int node = (int)(sk->joints[j] - d->nodes), at = -1;
            for (size_t q = 0; q < m.joints.size(); q++) if (m.joints[q] == node) { at = (int)q; break; }
#ifdef WM_MUT_ONE_SKIN
            if (si > 0) at = 0;       // only the first skin counts: the other meshes are bound to the wrong joints
#endif
            if (at < 0) {
                if (m.joints.size() + 2 > (size_t)kMaxJoints) return fail("too many joints (limit 255)");
                at = (int)m.joints.size(); m.joints.push_back(node); m.ibm.resize((at + 1) * 16);
                if (sk->inverse_bind_matrices) cgltf_accessor_read_float(sk->inverse_bind_matrices, j, &m.ibm[at * 16], 16); else Identity(&m.ibm[at * 16]);
            }
            skinJoint[si].push_back(at);
        }
    }
    // global bind transforms, for geometry that is not skinned
    std::vector<float> g(d->nodes_count * 16);
    for (size_t i = 0; i < d->nodes_count; i++) cgltf_node_transform_world(&d->nodes[i], &g[i * 16]);
    const int ident = (int)m.joints.size();
    // the model has one texture: the base colour image of the first textured material; the other materials are flat colour (their base colour factor)
    const cgltf_image *img = nullptr, *imgN = nullptr, *imgM = nullptr;
    for (size_t ni = 0; ni < d->nodes_count && !img; ni++) if (d->nodes[ni].mesh) for (size_t pi = 0; pi < d->nodes[ni].mesh->primitives_count && !img; pi++) {
        const cgltf_material* mt = d->nodes[ni].mesh->primitives[pi].material;
        if (mt && mt->has_pbr_metallic_roughness && mt->pbr_metallic_roughness.base_color_texture.texture) {
            img = mt->pbr_metallic_roughness.base_color_texture.texture->image; memcpy(m.baseColor, mt->pbr_metallic_roughness.base_color_factor, 16);
            if (mt->normal_texture.texture) imgN = mt->normal_texture.texture->image;
            if (mt->pbr_metallic_roughness.metallic_roughness_texture.texture) imgM = mt->pbr_metallic_roughness.metallic_roughness_texture.texture->image;
        }
    }
    for (size_t ni = 0; ni < d->nodes_count; ni++) {
        const cgltf_node* nd = &d->nodes[ni]; if (!nd->mesh) continue;
        bool skinned = nd->skin != nullptr; const std::vector<int>* sj = skinned ? &skinJoint[nd->skin - d->skins] : nullptr;
        for (size_t pi = 0; pi < nd->mesh->primitives_count; pi++) {
            const cgltf_primitive* p = &nd->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jt = nullptr, *wt = nullptr, *appearance = nullptr;
            for (size_t a = 0; a < p->attributes_count; a++) {
                const cgltf_attribute& at = p->attributes[a];
                if (at.name && !strcmp(at.name, "_APPEARANCE")) appearance = at.data;
                if (at.index != 0) continue;
                if (at.type == cgltf_attribute_type_position) pos = at.data; else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord) uv = at.data; else if (at.type == cgltf_attribute_type_joints) jt = at.data;
                else if (at.type == cgltf_attribute_type_weights) wt = at.data;
            }
            if (!pos) continue;
            float colf[4] = {1, 1, 1, 1}; bool samples = false;
            if (p->material && p->material->has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pb = p->material->pbr_metallic_roughness; memcpy(colf, pb.base_color_factor, 16);
                samples = img && pb.base_color_texture.texture && pb.base_color_texture.texture->image == img;
#ifdef WM_MUT_NO_FLAT_COLOR
                colf[0] = colf[1] = colf[2] = 1; samples = true;       // every material is taken for the textured one: flat colours are lost
#endif
            }
            uint32_t base = (uint32_t)m.verts.size(); bool useSkin = skinned && jt && wt;
            for (size_t i = 0; i < pos->count; i++) {
                Vertex v = {}; cgltf_accessor_read_float(pos, i, v.pos, 3);
                if (appearance && appearance->type == cgltf_type_scalar && appearance->count == pos->count) {
                    float group = 0; cgltf_accessor_read_float(appearance, i, &group, 1);
#ifndef WM_MUT_APPEARANCE_GROUP
                    if (std::isfinite(group) && group >= 0 && group <= 8 && std::floor(group) == group) v.appearanceGroup = group;
#else
                    v.appearanceGroup = group;
#endif
                }
                for (int k = 0; k < 3; k++) v.col[k] = (uint8_t)(std::min(1.0f, std::max(0.0f, colf[k])) * 255.0f + 0.5f);
                v.col[3] = samples ? 255 : 0;
                if (nrm) cgltf_accessor_read_float(nrm, i, v.nrm, 3); else v.nrm[1] = 1;
                if (uv) cgltf_accessor_read_float(uv, i, v.uv, 2);
                if (useSkin) {
                    cgltf_uint j4[4] = {0, 0, 0, 0}; cgltf_accessor_read_uint(jt, i, j4, 4); cgltf_accessor_read_float(wt, i, v.weights, 4);
                    float sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
                    for (int k = 0; k < 4; k++) { v.joints[k] = (uint8_t)((size_t)j4[k] < sj->size() ? (*sj)[j4[k]] : ident); if (sum > 0) v.weights[k] /= sum; }
                    if (sum <= 0) { v.joints[0] = (uint8_t)ident; v.weights[0] = 1; }
                } else {      // bake the node's bind transform and bind to the identity joint
                    const float* G = &g[ni * 16]; float p3[3] = {v.pos[0], v.pos[1], v.pos[2]}, n3[3] = {v.nrm[0], v.nrm[1], v.nrm[2]};
                    for (int k = 0; k < 3; k++) {
                        v.pos[k] = G[k] * p3[0] + G[4 + k] * p3[1] + G[8 + k] * p3[2] + G[12 + k];
                        v.nrm[k] = G[k] * n3[0] + G[4 + k] * n3[1] + G[8 + k] * n3[2];
                    }
                    v.joints[0] = (uint8_t)ident; v.weights[0] = 1;
                }
                m.verts.push_back(v);
            }
            if (p->indices) for (size_t i = 0; i < p->indices->count; i++) m.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(p->indices, i));
            else for (size_t i = 0; i < pos->count; i++) m.idx.push_back(base + (uint32_t)i);
        }
    }
    if (m.verts.empty() || m.idx.size() < 3) return fail("the file has no triangle mesh");
    auto decode = [](const cgltf_image* im, int* w, int* h, std::vector<uint8_t>* out) {
        if (!im || !im->buffer_view || !im->buffer_view->buffer->data) return;
        int c; const uint8_t* src = (const uint8_t*)im->buffer_view->buffer->data + im->buffer_view->offset;
        uint8_t* px = stbi_load_from_memory(src, (int)im->buffer_view->size, w, h, &c, 4);
        if (px) { out->assign(px, px + (size_t)*w * *h * 4); stbi_image_free(px); } else { *w = *h = 0; }
    };
    decode(img, &m.texW, &m.texH, &m.rgba); decode(imgN, &m.nrmW, &m.nrmH, &m.nrm); decode(imgM, &m.mrW, &m.mrH, &m.mr);
    for (size_t ai = 0; ai < d->animations_count; ai++) {
        const cgltf_animation* a = &d->animations[ai]; Clip c; c.name = a->name ? a->name : "";
        for (size_t ci = 0; ci < a->channels_count; ci++) {
            const cgltf_animation_channel& ch = a->channels[ci]; Channel o; o.node = (int)(ch.target_node - d->nodes);
            if (ch.target_path == cgltf_animation_path_type_translation) o.path = 0; else if (ch.target_path == cgltf_animation_path_type_rotation) o.path = 1;
            else if (ch.target_path == cgltf_animation_path_type_scale) o.path = 2; else continue;
            o.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;     // read as linear, using the key values and ignoring the tangents
            int comps = o.path == 1 ? 4 : 3;
            for (size_t k = 0; k < ch.sampler->input->count; k++) {
                float tm; cgltf_accessor_read_float(ch.sampler->input, k, &tm, 1); o.times.push_back(tm); c.duration = std::max(c.duration, tm);
                float v[4]; cgltf_accessor_read_float(ch.sampler->output, cubic ? k * 3 + 1 : k, v, comps); for (int q = 0; q < comps; q++) o.values.push_back(v[q]);
            }
            if (!o.times.empty()) c.channels.push_back(std::move(o));
        }
        if (c.duration > 0) m.clips.push_back(std::move(c));
    }
    const size_t nn=m.nodes.size();
    std::vector<float> bind(nn*16);
    for (size_t i=0;i<nn;++i) {
        std::vector<int> chain; for(int n=int(i);n>=0;n=m.nodes[n].parent) chain.push_back(n);
        float g[16]; Identity(g);
        for(auto n=chain.rbegin();n!=chain.rend();++n) { float l[16]; const auto& b=m.nodes[*n]; FromTRS(b.t,b.r,b.s,l); Mul(g,l,g); }
        memcpy(bind.data()+i*16,g,64);
    }
    m.gestureBone.assign(nn,-1); m.gestureForward.assign(nn,1);
    for(size_t i=0;i<nn;++i) {
        for(int b=0;b<GestureBones;++b) if(m.nodes[i].name==kGestureBones[b]) m.gestureBone[i]=b;
        m.gestureForward[i]=bind[i*16+10]>=0 ? 1.f : -1.f;
    }
    // bind-pose bounds
    std::vector<float> jm(kMaxJoints * 16); JointMatrices(m, -1, 0, jm.data());
    for (int k = 0; k < 3; k++) { m.bmin[k] = 1e30f; m.bmax[k] = -1e30f; }
    for (const Vertex& v : m.verts) { float p[3]; SkinPosition(m, jm.data(), v, p); for (int k = 0; k < 3; k++) { m.bmin[k] = std::min(m.bmin[k], p[k]); m.bmax[k] = std::max(m.bmax[k], p[k]); } }
    *out = std::move(m);
    return true;
}

// ---- animation
static void Sample(const Channel& c, float t, float* out) {
    int comps = c.path == 1 ? 4 : 3; size_t n = c.times.size();
    if (t <= c.times[0] || n == 1) { memcpy(out, &c.values[0], comps * 4); return; }
    if (t >= c.times[n - 1]) { memcpy(out, &c.values[(n - 1) * comps], comps * 4); return; }
    size_t hi = std::upper_bound(c.times.begin(), c.times.end(), t) - c.times.begin(), lo = hi - 1;
    if (c.step) { memcpy(out, &c.values[lo * comps], comps * 4); return; }
    float f = (t - c.times[lo]) / (c.times[hi] - c.times[lo]);
    const float *a = &c.values[lo * comps], *b = &c.values[hi * comps];
    if (c.path == 1) Slerp(a, b, f, out); else for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * f;
}

static void PoseTRS(const Model& m, int clip, float t, float* T, float* R, float* S) {      // rest pose, then the clip's channels at t (looped)
    size_t nn = m.nodes.size();
    for (size_t i = 0; i < nn; i++) { memcpy(&T[i * 3], m.nodes[i].t, 12); memcpy(&R[i * 4], m.nodes[i].r, 16); memcpy(&S[i * 3], m.nodes[i].s, 12); }
    if (clip >= 0 && clip < (int)m.clips.size()) {
        const Clip& c = m.clips[clip];
#ifndef WM_MUT_NO_LOOP
        t = c.duration > 0 ? t - c.duration * floorf(t / c.duration) : 0;
#endif
        for (const Channel& ch : c.channels) { float v[4]; Sample(ch, t, v); float* dst = ch.path == 0 ? &T[ch.node * 3] : ch.path == 1 ? &R[ch.node * 4] : &S[ch.node * 3]; memcpy(dst, v, (ch.path == 1 ? 4 : 3) * 4); }
    }
}

static void QMul(const float* a, const float* b, float* o) {
    float r[4] = {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1], a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0], a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3], a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]}; memcpy(o, r, 16);
}
// ---- the life layer's rig: which node is what, and which local axis of it does what in model space
static void BindGlobal(const Model& m, size_t i, float* G) {      // the node's bind transform in model space
    std::vector<int> chain; for (int n = (int)i; n >= 0; n = m.nodes[n].parent) chain.push_back(n);
    Identity(G); for (auto n = chain.rbegin(); n != chain.rend(); ++n) { float l[16]; const Node& b = m.nodes[*n]; FromTRS(b.t, b.r, b.s, l); Mul(G, l, G); }
}
static void Rot3(const float* G, const float* v, float* o) { for (int r = 0; r < 3; r++) o[r] = G[r] * v[0] + G[4 + r] * v[1] + G[8 + r] * v[2]; }
static void Rot3T(const float* G, const float* v, float* o) { for (int c = 0; c < 3; c++) o[c] = G[c * 4] * v[0] + G[c * 4 + 1] * v[1] + G[c * 4 + 2] * v[2]; }
// The local axis (and the sign) whose rotation moves the node's probe direction (given in model space) toward `want` (model space).
static LifeAxis ProbeAxis(const float* G, const float* probeModel, const float* want) {
    float Gn[16]; memcpy(Gn, G, 64);
    for (int c = 0; c < 3; c++) { float l = sqrtf(Gn[c*4]*Gn[c*4] + Gn[c*4+1]*Gn[c*4+1] + Gn[c*4+2]*Gn[c*4+2]); if (l > 1e-9f) for (int r = 0; r < 3; r++) Gn[c*4+r] /= l; }
    float v[3]; Rot3T(Gn, probeModel, v); LifeAxis best; float bestScore = 0;
    for (int k = 0; k < 3; k++) {
        float e[3] = {0, 0, 0}; e[k] = 1; float c[3] = {e[1]*v[2]-e[2]*v[1], e[2]*v[0]-e[0]*v[2], e[0]*v[1]-e[1]*v[0]}, d[3]; Rot3(Gn, c, d);
        float score = d[0]*want[0] + d[1]*want[1] + d[2]*want[2];
        if (fabsf(score) > fabsf(bestScore)) { bestScore = score; best.axis = k; best.sign = score >= 0 ? 1.0f : -1.0f; }
    }
    if (fabsf(bestScore) < 0.2f) best.axis = -1;      // no local axis does it
    return best;
}
static int NodeNamed(const Model& m, const char* name) { for (size_t i = 0; i < m.nodes.size(); i++) if (m.nodes[i].name == name) return (int)i; return -1; }
static void BindChain(const Model& m, LifeChain& c, const char* prefix, int count) {
    const float fwd[3] = {0, 0, 1}, left[3] = {1, 0, 0}; c.count = 0;
    for (int i = 0; i < count && i < 3; i++) {
        char nm[48]; snprintf(nm, sizeof nm, "%s.%d", prefix, i + 1); int n = NodeNamed(m, nm); if (n < 0) n = i == 0 ? NodeNamed(m, prefix) : -1; if (n < 0) break;
        float G[16]; BindGlobal(m, n, G); float axis[3] = {0, 1, 0}, down[3]; Rot3(G, axis, down);      // the bone runs along its local Y
        float l = sqrtf(down[0]*down[0] + down[1]*down[1] + down[2]*down[2]); if (l > 1e-9f) for (float& v : down) v /= l;
        c.node[i] = n; c.swing[i] = ProbeAxis(G, down, fwd); c.sway[i] = ProbeAxis(G, down, left); c.count = i + 1;
    }
    for (int i = 0; i < c.count; i++) {
        int child = i + 1 < c.count ? c.node[i + 1] : -1; float len = 0;
        if (child >= 0) { const float* t = m.nodes[child].t; len = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]); } else if (i > 0) len = c.length[i - 1];
        c.length[i] = len > 0.01f ? len : 0.1f;
    }
}
void LifeRig::Bind(const Model& m, float lidCloseScale) {
    const float fwd[3] = {0, 0, 1}, left[3] = {1, 0, 0}, up[3] = {0, 1, 0};
    head = NodeNamed(m, "head"); neck = NodeNamed(m, "neck"); chest = NodeNamed(m, "chest"); eye[0] = NodeNamed(m, "eye.L"); eye[1] = NodeNamed(m, "eye.R"); lid[0] = NodeNamed(m, "lid.L"); lid[1] = NodeNamed(m, "lid.R");
    lidClose = lidCloseScale > 1.0f && lidCloseScale < 4.0f ? lidCloseScale : 1.0f; ok = head >= 0;
    float G[16];
    if (head >= 0) { BindGlobal(m, head, G); headYaw = ProbeAxis(G, fwd, left); headPitch = ProbeAxis(G, fwd, up); }
    if (neck >= 0) { BindGlobal(m, neck, G); neckYaw = ProbeAxis(G, fwd, left); neckPitch = ProbeAxis(G, fwd, up); }
    if (chest >= 0) { BindGlobal(m, chest, G); chestPitch = ProbeAxis(G, fwd, up); }
    for (int e = 0; e < 2; e++) if (eye[e] >= 0) { BindGlobal(m, eye[e], G); eyeYaw[e] = ProbeAxis(G, fwd, left); eyePitch[e] = ProbeAxis(G, fwd, up); }
    BindChain(m, scarfA, "scarf_a", 3); BindChain(m, scarfB, "scarf_b", 2); BindChain(m, canteen, "canteen", 1); BindChain(m, satchel, "satchel", 1);
}
static void RotLocal(float* q, const LifeAxis& ax, float angle) {
    if (ax.axis < 0 || angle == 0) return;
    float r[4] = {0, 0, 0, 1}; r[ax.axis] = sinf(ax.sign * angle * 0.5f); r[3] = cosf(ax.sign * angle * 0.5f); QMul(q, r, q);
}
static void ApplyLife(const LifeRig& rig, const LifeState& L, float* R, float* S) {
    const float hw = L.headWeight;
    if (rig.head >= 0) { RotLocal(&R[rig.head * 4], rig.headYaw, 0.65f * L.headYaw * hw); RotLocal(&R[rig.head * 4], rig.headPitch, 0.65f * L.headPitch * hw - 0.004f * L.breath); }
    if (rig.neck >= 0) { RotLocal(&R[rig.neck * 4], rig.neckYaw, 0.35f * L.headYaw * hw); RotLocal(&R[rig.neck * 4], rig.neckPitch, 0.35f * L.headPitch * hw - 0.006f * L.breath); }
    if (rig.chest >= 0) RotLocal(&R[rig.chest * 4], rig.chestPitch, 0.012f * L.breath);      // breathing in lifts the chest
    for (int e = 0; e < 2; e++) {
        if (rig.eye[e] >= 0) { RotLocal(&R[rig.eye[e] * 4], rig.eyeYaw[e], L.eyeYaw); RotLocal(&R[rig.eye[e] * 4], rig.eyePitch[e], L.eyePitch); }
        if (rig.lid[e] >= 0) S[rig.lid[e] * 3 + 1] *= 1 + (rig.lidClose - 1) * L.blink;      // the lid bone stretches along its length (local Y)
    }
    auto chain = [&](const LifeChain& c, const LifePendulum* p) { for (int i = 0; i < c.count; i++) { RotLocal(&R[c.node[i] * 4], c.swing[i], p[i].a); RotLocal(&R[c.node[i] * 4], c.sway[i], p[i].b); } };
    chain(rig.scarfA, L.scarfA); chain(rig.scarfB, L.scarfB); chain(rig.canteen, &L.canteen); chain(rig.satchel, &L.satchel);
}

void JointMatricesBlend(const Model& m, int from, float tFrom, int to, float tTo, float w, float* out, const GestureState* gesture, float radioRaise, float* radioHand) {
    PoseLayers l; l.gesture = gesture; l.radioRaise = radioRaise; l.radioHand = radioHand; JointMatricesBlendEx(m, from, tFrom, to, tTo, w, out, l);
}
void JointMatricesBlendEx(const Model& m, int from, float tFrom, int to, float tTo, float w, float* out, const PoseLayers& layers) {
    const GestureState* gesture = layers.gesture; float radioRaise = layers.radioRaise; float* radioHand = layers.radioHand;
    size_t nn = m.nodes.size();
    std::vector<float> T(nn * 3), R(nn * 4), S(nn * 3);
    PoseTRS(m, to, tTo, T.data(), R.data(), S.data());
    if (from >= 0 && w < 1) {
        std::vector<float> T0(nn * 3), R0(nn * 4), S0(nn * 3); PoseTRS(m, from, tFrom, T0.data(), R0.data(), S0.data());
        if (w < 0) w = 0;
        for (size_t i = 0; i < nn * 3; i++) { T[i] = T0[i] + (T[i] - T0[i]) * w; S[i] = S0[i] + (S[i] - S0[i]) * w; }
        for (size_t i = 0; i < nn; i++) { float q[4]; Slerp(&R0[i * 4], &R[i * 4], w, q); memcpy(&R[i * 4], q, 16); }
    }
    if (gesture) {
        // Bind global rotations resolve forward-positive angles on both mirrored
        // limbs. The generator uses the same bind-axis probe for its gait.
        auto qmul=[](const float* a,const float* b,float* o) {
            float r[4]={a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]}; memcpy(o,r,16);
        };
        for(size_t i=0;i<nn;++i) if (i<m.gestureBone.size() && m.gestureBone[i]>=0) {
            int b=m.gestureBone[i];
            const auto& j=gesture->pose[b]; if(j.weight <= .00001f) continue;
            float q[4]={0,0,0,1};
            float fwd=m.gestureForward[i];
            // Blender's XYZ Euler convention: qz * qy * qx.
            for(int a=2;a>=0;--a) { float r[4]={0,0,0,1}; float ang=j.a[a]*.00872664626f*(a==0?fwd:1.f); r[a]=sinf(ang); r[3]=cosf(ang); qmul(q,r,q); }
            qmul(m.nodes[i].r,q,q); float result[4]; Slerp(R.data()+i*4,q,j.weight,result); memcpy(R.data()+i*4,result,16);
        }
        for(size_t i=0;i<nn;++i) if(m.nodes[i].name=="root") {
            float q[4]={0,sinf(gesture->turn*.5f),0,cosf(gesture->turn*.5f)};
            qmul(R.data()+i*4,q,R.data()+i*4);
        }
        // Seated pelvis height follows the leg chain rather than a fixed drop.
        // The knee folds while the hip lowers; soles stay above the floor.
        float hip=gesture->pose[ThighL].a[0]*gesture->pose[ThighL].weight*.01745329252f;
        float knee=gesture->pose[ShinL].a[0]*gesture->pose[ShinL].weight*.01745329252f;
        float drop=.43f*(1-cosf(hip))+.41f*(1-cosf(hip+knee));
        for(size_t i=0;i<nn;++i) if(m.nodes[i].name=="pelvis") T[i*3+1]-=drop;
    }
    // Independent right-arm overlay. The other arm and all lower-body animation
    // retain their sampled pose; a proximity talking overlay can share this slot.
    if (radioRaise > 0) for (size_t i=0;i<nn;++i) {
        float angle = m.nodes[i].name == "upper_arm.R" ? -1.25f : m.nodes[i].name == "forearm.R" ? -2.25f : 0;
        if (!angle) continue;
        float q[4] = {std::sin(angle*.5f),0,0,std::cos(angle*.5f)}, target[4];
        const float* b=m.nodes[i].r;
        target[0]=b[3]*q[0]+b[0]*q[3]; target[1]=b[1]*q[3]+b[2]*q[0];
        target[2]=b[2]*q[3]-b[1]*q[0]; target[3]=b[3]*q[3]-b[0]*q[0];
#ifdef WR_MUT_POSE
        (void)target;
#else
        float blended[4]; Slerp(&R[i*4],target,std::min(1.0f,radioRaise),blended); memcpy(&R[i*4],blended,16);
#endif
    }
    if (layers.life && layers.rig && layers.rig->ok) ApplyLife(*layers.rig, *layers.life, R.data(), S.data());
    std::vector<float> G(nn * 16); std::vector<char> done(nn, 0);
    struct Rec { static void Go(const Model& m, size_t i, const float* T, const float* R, const float* S, float* G, char* done) {
        if (done[i]) return;
        float l[16]; FromTRS(&T[i * 3], &R[i * 4], &S[i * 3], l);
        if (m.nodes[i].parent >= 0) { Go(m, m.nodes[i].parent, T, R, S, G, done); Mul(&G[m.nodes[i].parent * 16], l, &G[i * 16]); } else memcpy(&G[i * 16], l, 64);
        done[i] = 1; } };
    for (size_t i = 0; i < nn; i++) Rec::Go(m, i, T.data(), R.data(), S.data(), G.data(), done.data());
    if (radioHand) { Identity(radioHand); for (size_t i=0;i<nn;++i) if (m.nodes[i].name == "hand.R") memcpy(radioHand,&G[i*16],64); }
    for (size_t j = 0; j < m.joints.size(); j++) {
#ifdef WM_MUT_IBM_ORDER
        Mul(&m.ibm[j * 16], &G[m.joints[j] * 16], out + j * 16);
#else
        Mul(&G[m.joints[j] * 16], &m.ibm[j * 16], out + j * 16);
#endif
    }
    Identity(out + m.joints.size() * 16);
}

void JointMatrices(const Model& m, int clip, float t, float* out) { JointMatricesBlend(m, -1, 0, clip, t, 1, out); }

void SkinPosition(const Model&, const float* jm, const Vertex& v, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int k = 0; k < 4; k++) {
        float w = v.weights[k]; if (w <= 0) continue; const float* M = jm + v.joints[k] * 16;
        for (int r = 0; r < 3; r++) out[r] += w * (M[r] * v.pos[0] + M[4 + r] * v.pos[1] + M[8 + r] * v.pos[2] + M[12 + r]);
    }
}

int FindClip(const Model& m, const char* word) {
    std::string w(word); for (char& c : w) c = (char)tolower((unsigned char)c);
    for (size_t i = 0; i < m.clips.size(); i++) { std::string n = m.clips[i].name; for (char& c : n) c = (char)tolower((unsigned char)c); if (n.find(w) != std::string::npos) return (int)i; }
    return -1;
}

Fit FitToHeight(const Model& m, float height) {
    Fit f; float h = m.bmax[1] - m.bmin[1]; f.scale = h > 1e-6f ? height / h : 1;
    f.offset[0] = (m.bmin[0] + m.bmax[0]) * 0.5f; f.offset[1] = m.bmin[1]; f.offset[2] = (m.bmin[2] + m.bmax[2]) * 0.5f;
    return f;
}

// Translation of the inverse of the affine matrix M (column-major): where a joint's own origin sits in model space in the bind pose.
static bool InverseOrigin(const float* M, float o[3]) {
    float a = M[0], b = M[4], c = M[8], d = M[1], e = M[5], f = M[9], g = M[2], h = M[6], i = M[10];      // rows of the 3x3 part
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g); if (fabsf(det) < 1e-20f) return false;
    float inv[9] = {(e * i - f * h), -(b * i - c * h), (b * f - c * e), -(d * i - f * g), (a * i - c * g), -(a * f - c * d), (d * h - e * g), -(a * h - b * g), (a * e - b * d)};
    float t[3] = {-M[12], -M[13], -M[14]};
    for (int r = 0; r < 3; r++) o[r] = (inv[r * 3] * t[0] + inv[r * 3 + 1] * t[1] + inv[r * 3 + 2] * t[2]) / det;
    return true;
}
float EyeHeightPosed(const Model& m, const float* jm, const char* nameContains) {
    // Where the eye joints are in the space bmin/bmax are measured in: the joint's own origin (translation of the inverse of its inverse bind matrix), put through
    // the skin matrix of the pose. (The two spaces differ by whatever scale the nodes above the skeleton carry: a parent node can scale them 100 times apart.)
    if (!nameContains || !*nameContains) return kNoEye;
#ifdef WM_MUT_EYE_BIND
    std::vector<float> bind(kMaxJoints * 16); JointMatrices(m, -1, 0, bind.data()); jm = bind.data();      // always the bind pose: the eyes never move with the animation
#endif
    float sum = 0; int n = 0;
    for (size_t j = 0; j < m.joints.size(); j++) {
        if (m.nodes[m.joints[j]].name.find(nameContains) == std::string::npos) continue;
        float o[3];
        if (!InverseOrigin(&m.ibm[j * 16], o)) continue;
#ifdef WM_MUT_EYE_IBM
        sum += o[1]; n++; continue;            // the origin in the inverse bind matrix's space: wrong whenever that space is not the one the bounds are in
#endif
        const float* M = &jm[j * 16]; sum += M[1] * o[0] + M[5] * o[1] + M[9] * o[2] + M[13]; n++;
    }
    return n ? sum / n : kNoEye;
}
float EyeHeight(const Model& m, const char* nameContains) {
    std::vector<float> jm(kMaxJoints * 16); JointMatrices(m, -1, 0, jm.data()); return EyeHeightPosed(m, jm.data(), nameContains);
}
Fit FitToEye(const Model& m, float eye, const char* nameContains) {
    float ey = EyeHeight(m, nameContains), h = m.bmax[1] - m.bmin[1], above = ey - m.bmin[1];
#ifdef WM_MUT_EYE_NEG
    if (ey < 0 || above < 1e-6f) return FitToHeight(m, eye / 0.92f);          // the old test: a negative height was taken for "no eye joints"
#else
    if (ey <= kNoEye * 0.5f || above < 1e-6f) return FitToHeight(m, eye / 0.92f);
#endif
    Fit f = FitToHeight(m, h * eye / above);
    return f;
}

// ---- camera
bool ProjectPoint(const Cam& c, const float* w, float* nx, float* ny) {
    float d[3] = {w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2]}, th = c.theta, ph = c.phi;
    float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
    float f = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
    if (f < 0.1f) return false;
    float tv = tanf(c.vfovDeg * 3.14159265f / 360.0f), tu = tv * c.aspect;
    *nx = ((d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / f) / tu;
    *ny = ((d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / f) / tv;
    return true;
}

void ViewProj(const Cam& c, float out[16]) {
    float th = c.theta, ph = c.phi;
    float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
    float tv = tanf(c.vfovDeg * 3.14159265f / 360.0f), tu = tv * c.aspect, n = c.nearZ, f = c.farZ;
    float A = f / (f - n), B = -f * n / (f - n);
    // clip.x = (d.right) / tu, clip.y = (d.up) / tv, clip.w = d.fwd, clip.z = A * d.fwd + B, with d = world - cam
    float rows[4][4];
    for (int k = 0; k < 3; k++) { rows[0][k] = right[k] / tu; rows[1][k] = up[k] / tv; rows[2][k] = A * fwd[k]; rows[3][k] = fwd[k]; }
    for (int r = 0; r < 4; r++) {
        float tr = 0; for (int k = 0; k < 3; k++) tr -= rows[r][k] * c.pos[k];
        rows[r][3] = tr + (r == 2 ? B : 0);
    }
    for (int r = 0; r < 4; r++) for (int col = 0; col < 4; col++) out[col * 4 + r] = rows[r][col];
}

void ModelMatrix(const float feet[3], float yaw, const Fit& f, float yawOffset, float out[16]) {
    // model space: x = the model's left, y = up, z = front. World: front = heading, left = (-sin, cos, 0), up = z.
    float a = yaw + yawOffset, fx = cosf(a), fy = sinf(a);
#ifdef WM_MUT_MIRROR
    float lx = sinf(a), ly = -cosf(a);
#else
    float lx = -sinf(a), ly = cosf(a);
#endif
    float s = f.scale;
    float cx[3] = {lx * s, ly * s, 0}, cy[3] = {0, 0, s}, cz[3] = {fx * s, fy * s, 0};
    float tx = -(cx[0] * f.offset[0] + cy[0] * f.offset[1] + cz[0] * f.offset[2]) + feet[0];
    float ty = -(cx[1] * f.offset[0] + cy[1] * f.offset[1] + cz[1] * f.offset[2]) + feet[1];
    float tz = -(cx[2] * f.offset[0] + cy[2] * f.offset[1] + cz[2] * f.offset[2]) + feet[2];
    float m[16] = {cx[0], cx[1], cx[2], 0, cy[0], cy[1], cy[2], 0, cz[0], cz[1], cz[2], 0, tx, ty, tz, 1};
    memcpy(out, m, 64);
}

// ---- avatar
void Avatar::Update(uint64_t nowMs, bool valid, unsigned seq, const float pos[3], float yaw) {
#ifdef WM_MUT_NO_TELEPORT_GUARD
    const bool guardTeleport = false;
#else
    const bool guardTeleport = true;
#endif
    if (!valid) { have_ = false; speed_ = 0; return; }
    float dt = have_ ? (float)(nowMs - lastMs_) / 1000.0f : 0; lastMs_ = nowMs;
    if (!have_) { memcpy(target_, pos, 12); memcpy(pos_, pos, 12); memcpy(lastTarget_, pos, 12); lastSeq_ = seq; lastSeqMs_ = nowMs; yaw_ = yaw; have_ = true; speed_ = 0; return; }
    if (seq != lastSeq_) {                  // a new datagram: measure the speed over the time since the previous one
        float dx = pos[0] - lastTarget_[0], dy = pos[1] - lastTarget_[1], dz = pos[2] - lastTarget_[2];
        float dist = sqrtf(dx * dx + dy * dy), tm = (float)(nowMs - lastSeqMs_) / 1000.0f;
        if (guardTeleport && (dist > 4.0f || fabsf(dz) > 4.0f)) { memcpy(pos_, pos, 12); speed_ = 0; }                 // a teleport or a reload: jump, do not run there
        else if (tm > 0.001f) { float inst = dist / tm; float k = 1 - expf(-tm / g_.tau); speed_ += (inst - speed_) * k; }
        memcpy(lastTarget_, pos, 12); lastSeq_ = seq; lastSeqMs_ = nowMs;
    } else if ((nowMs - lastSeqMs_) > 250) speed_ *= expf(-dt / g_.tau);                           // no news: stop walking
    memcpy(target_, pos, 12);
    float k = dt > 0 ? 1 - expf(-dt / 0.05f) : 0;                    // position smoothing between the 30 Hz datagrams
    for (int i = 0; i < 3; i++) pos_[i] += (target_[i] - pos_[i]) * k;
    yaw_ = yaw;
    idlePhase_ += dt;
    if (speed_ >= g_.idleBelow) { phase_ += dt * speed_ / g_.refSpeed; runPhase_ += dt * speed_ / (g_.runRef > 0 ? g_.runRef : g_.refSpeed); }
#ifdef WM_MUT_NO_RUNREF
    runPhase_ = phase_;
#endif
}

static bool SameNoCase(const std::string& a, const char* b) { size_t i = 0; for (; i < a.size() && b[i]; ++i) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false; return i == a.size() && !b[i]; }
static int ClipExact(const Model& m, const char* name) { for (size_t i = 0; i < m.clips.size(); ++i) if (SameNoCase(m.clips[i].name, name)) return (int)i; return -1; }
int EmoteClip(const Model& m, int emote) {
    int c = -1;
    switch (emote) {
    case 1: c = FindClip(m, "wave"); break;
    case 2: c = FindClip(m, "dance"); break;
    case 3: c = FindClip(m, "thumbs"); if (c < 0) c = FindClip(m, "jump"); break;
    case 4: c = ClipExact(m, "yes"); if (c < 0) c = FindClip(m, "agree"); break;
    case 5:
#ifdef WM_MUT_EMOTE_SUBSTRING
        c = FindClip(m, "no"); break;          // any clip with "no" in its name: Snow, Annotate, ...
#else
        c = ClipExact(m, "no"); if (c < 0) c = FindClip(m, "headshake"); break;
#endif
    }
    return c;
}

float Avatar::TimeOf(const Model& m, int kind, int clip) const {
    if (kind == 4) { float t = (float)(lastMs_ - fidgetStartMs_) / 1000.0f, d = clip >= 0 && clip < (int)m.clips.size() ? m.clips[clip].duration : 0; return d > 0 ? std::min(t, d - 0.001f) : t; }
    return kind == 1 ? phase_ : kind == 2 ? runPhase_ : kind == 0 ? idlePhase_ : 0.0f;      // kind 3: a held first frame
}
Avatar::Sel Avatar::Choose(const Model& m) const {
    int idle = FindClip(m, "idle"); if (idle < 0) idle = FindClip(m, "stand"); if (idle < 0) idle = FindClip(m, "survey");
    int walk = FindClip(m, "walk"); int run = FindClip(m, "run"); if (run < 0) run = FindClip(m, "sprint");
    if (walk < 0) walk = idle >= 0 && m.clips.size() > 1 ? (idle == 0 ? 1 : 0) : 0;      // an unnamed clip is the walk cycle
    const bool still = speed_ < g_.idleBelow;
#ifdef WM_MUT_FIDGET_MOVING
    if (fidgetClip_ >= 0 && busy_) fidgetClip_ = -1;
#else
    if (fidgetClip_ >= 0 && (!still || busy_)) fidgetClip_ = -1;      // walking off or an emote ends the fidget (it fades into what comes next)
#endif
    if (!still) idleSinceMs_ = 0;
    if (fidgetClip_ >= 0) {
        if ((float)(lastMs_ - fidgetStartMs_) / 1000.0f >= m.clips[fidgetClip_].duration - 0.05f) { fidgetClip_ = -1; idleSinceMs_ = lastMs_; fidgetDue_ = 8.0f + 14.0f * Rand(); }
        else return Sel{fidgetClip_, 4};
    }
    if (still && idle >= 0 && !busy_) {
        if (!idleSinceMs_) idleSinceMs_ = lastMs_;
#ifndef WM_MUT_NO_FIDGET
        if ((float)(lastMs_ - idleSinceMs_) / 1000.0f >= fidgetDue_) {
            std::vector<int> fidgets; for (size_t i = 0; i < m.clips.size(); i++) if (m.clips[i].name.rfind("fidget", 0) == 0 && m.clips[i].duration > 0.2f) fidgets.push_back((int)i);
            if (!fidgets.empty()) { fidgetClip_ = fidgets[std::min(fidgets.size() - 1, (size_t)(Rand() * fidgets.size()))]; fidgetStartMs_ = lastMs_; return Sel{fidgetClip_, 4}; }
            fidgetDue_ = 1e9f;      // no fidget clips: stop asking
        }
#endif
    }
    if (still) return idle >= 0 ? Sel{idle, 0} : Sel{walk, 3};
#ifndef WM_MUT_NO_RUN
    // Retain running through small packet-timing fluctuations at the boundary.
    // Repeatedly restarting a walk/run fade produces a visible upper-body pop.
    if (run >= 0 && speed_ >= g_.runSpeed * (last_.kind == 2 ? .9f : 1.f)) return {run, 2};
#endif
    return {walk, 1};
}

ClipPick Avatar::Pick(const Model& m) const {
    if (m.clips.empty()) return {-1, 0};
    Sel cur = Choose(m);
    if (last_.clip >= 0 && cur.clip != last_.clip) { prev_ = last_; fadeStartMs_ = lastMs_; }        // the state changed: fade from what was playing
    last_ = cur;
    ClipPick pk{cur.clip, TimeOf(m, cur.kind, cur.clip)};
#ifndef WM_MUT_NO_BLEND
    if (prev_.clip >= 0 && g_.fadeMs > 0) {
        float w = (float)(lastMs_ - fadeStartMs_) / g_.fadeMs;
        if (w < 1) { pk.from = prev_.clip; pk.fromTime = TimeOf(m, prev_.kind, prev_.clip); pk.blend = w < 0 ? 0 : w; }
    }
#endif
    return pk;
}

}  // namespace wm
