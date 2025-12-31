#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

#include <cctype>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include "glm/ext/matrix_transform.hpp"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include "orbitcamera.h"
#include <glm/gtc/quaternion.hpp>

static int fix_obj_index(int idx, int count) {
    // OBJ:  1..count  (positive)
    //       -1..-count (negative, relative to end)
    // We return 0-based index, or -1 if invalid/zero.
    if (idx > 0) return idx - 1;
    if (idx < 0) return count + idx;   // e.g. -1 => last element
    return -1;
}

static std::vector<float> load_obj(const std::string& path)
{
    std::vector<float> out;   // flat list: px py pz nx ny nz, triangulated
    std::vector<float> verts; // flat xyzxyz...
    std::vector<float> norms; // flat xyzxyz...

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ file: " << path << "\n";
        return out;
    }

    auto parse_tok = [&](const std::string& t) -> std::pair<int,int> {
        // returns (vi, ni) as 0-based indices; ni = -1 if missing
        int vi_raw = 0, ni_raw = 0;

        // Token formats:
        // v
        // v/vt
        // v//vn
        // v/vt/vn
        //
        // We only care about v and vn.
        size_t s1 = t.find('/');
        if (s1 == std::string::npos) {
            vi_raw = std::stoi(t);
        } else {
            vi_raw = std::stoi(t.substr(0, s1));

            size_t s2 = t.find('/', s1 + 1);
            if (s2 != std::string::npos) {
                // there is a vn field (maybe empty between //)
                if (s2 + 1 < t.size()) {
                    std::string vn_part = t.substr(s2 + 1);
                    if (!vn_part.empty())
                        ni_raw = std::stoi(vn_part);
                }
            }
            // if only v/vt, no normal
        }

        int vcount = static_cast<int>(verts.size() / 3);
        int ncount = static_cast<int>(norms.size() / 3);

        int vi = fix_obj_index(vi_raw, vcount);
        int ni = (ni_raw != 0) ? fix_obj_index(ni_raw, ncount) : -1;

        return {vi, ni};
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        // trim leading whitespace
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
            ++start;
        if (start >= line.size() || line[start] == '#')
            continue;

        std::istringstream iss(line.substr(start));
        std::string type;
        iss >> type;

        if (type == "v") {
            float x, y, z;
            if (iss >> x >> y >> z) {
                verts.push_back(x);
                verts.push_back(y);
                verts.push_back(z);
            }
        }
        else if (type == "vn") {
            float x, y, z;
            if (iss >> x >> y >> z) {
                norms.push_back(x);
                norms.push_back(y);
                norms.push_back(z);
            }
        }
        else if (type == "f") {
            std::vector<std::string> face;
            std::string tok;
            while (iss >> tok)
                face.push_back(tok);

            if (face.size() < 3)
                continue;

            // fan triangulation: (0, i, i+1)
            auto [v0i, n0i] = parse_tok(face[0]);
            if (v0i < 0) continue;

            for (size_t i = 1; i + 1 < face.size(); ++i) {
                auto [v1i, n1i] = parse_tok(face[i]);
                auto [v2i, n2i] = parse_tok(face[i + 1]);
                if (v1i < 0 || v2i < 0) continue;

                const int vis[3] = { v0i, v1i, v2i };
                const int nis[3] = { n0i, n1i, n2i };

                for (int k = 0; k < 3; ++k) {
                    int vo = vis[k] * 3;
                    float px = verts[vo + 0];
                    float py = verts[vo + 1];
                    float pz = verts[vo + 2];

                    float nx = 0.f, ny = 0.f, nz = 0.f;
                    if (nis[k] >= 0) {
                        int no = nis[k] * 3;
                        if (no + 2 < (int)norms.size()) {
                            nx = norms[no + 0];
                            ny = norms[no + 1];
                            nz = norms[no + 2];
                        }
                    }

                    out.push_back(px);
                    out.push_back(py);
                    out.push_back(pz);
                    out.push_back(nx);
                    out.push_back(ny);
                    out.push_back(nz);
                }
            }
        }
    }

    return out;
}

static std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void glfw_error_callback(int err, const char* msg) {
  std::cerr << "GLFW error " << err << ": " << msg << "\n";
}

static GLuint compileShader(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);

  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::string log(len, '\0');
    glGetShaderInfoLog(s, len, nullptr, log.data());
    std::cerr << "Shader compile error:\n" << log << "\n";
  }
  return s;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    std::string log(len, '\0');
    glGetProgramInfoLog(p, len, nullptr, log.data());
    std::cerr << "Program link error:\n" << log << "\n";
  }

  glDetachShader(p, vs);
  glDetachShader(p, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}

static GLuint createProgram(std::string vsPath, std::string fsPath){
    std::string vsString = read_text_file(vsPath);
    const char* vsSrc = vsString.c_str();
    std::string fsString = read_text_file(fsPath);
    const char* fsSrc = fsString.c_str();
    return linkProgram(compileShader(GL_VERTEX_SHADER, vsSrc),
                              compileShader(GL_FRAGMENT_SHADER, fsSrc));
}

struct RenderObj{
    std::string name;
    GLuint prog;
    GLuint vao, vbo;
    int vertex_count;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    glm::vec3 color;
};

struct SceneFBO {
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    int w = 0, h = 0;
};

struct Scene{
    GLuint prog;
    std::vector<RenderObj> renderObjs;
    OrbitCamera orbitCamera;
    glm::vec3 lightPos;
    glm::vec3 animLight;
    int selected;
};

static glm::mat4 renderobject_model(RenderObj *renderObj){
    glm::mat4 trans = glm::translate(glm::mat4(1.0), renderObj->position);
    glm::vec3 eulerRad = glm::radians(renderObj->rotation);
    glm::mat4 rot = glm::eulerAngleXYZ(eulerRad.x, eulerRad.y, eulerRad.z);
    glm::mat4 scale = glm::scale(glm::mat4(1.0), renderObj->scale);
    return trans * rot * scale;
}

static void create_render_object(Scene *scene, std::string modelPath, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale, glm::vec3 color){
    std::vector<float> vertices = load_obj(modelPath);
    GLuint vao=0, vbo=0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    RenderObj renderObj;
    renderObj.name = modelPath;
    renderObj.prog = scene->prog;
    renderObj.vao = vao;
    renderObj.vbo = vbo;
    renderObj.vertex_count = vertices.size() / 6;
    renderObj.position = position;
    renderObj.rotation = rotation;
    renderObj.scale = scale;
    renderObj.color = color;
    scene->renderObjs.push_back(renderObj);
}

static void render_object(RenderObj *renderObj, glm::mat4 view, glm::mat4 proj, glm::vec3 lightPos, glm::vec3 camPos){
    glUseProgram(renderObj->prog);
    const GLint locModel = glGetUniformLocation(renderObj->prog, "uModel");
    const GLint locView  = glGetUniformLocation(renderObj->prog, "uView");
    const GLint locProj  = glGetUniformLocation(renderObj->prog, "uProj");
    const GLint locLightPos = glGetUniformLocation(renderObj->prog, "uLightPos");
    const GLint locViewPos  = glGetUniformLocation(renderObj->prog, "uViewPos");
    const GLint locObjCol   = glGetUniformLocation(renderObj->prog, "uObjectColor");
    const GLint locLightCol = glGetUniformLocation(renderObj->prog, "uLightColor");

    glm::mat4 model = renderobject_model(renderObj);
    glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(locView,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(locProj,  1, GL_FALSE, glm::value_ptr(proj));

    glUniform3fv(locLightPos, 1, glm::value_ptr(lightPos));
    glUniform3fv(locViewPos,  1, glm::value_ptr(camPos));

    glm::vec3 lightColor (1.0f, 1.0f, 1.0f);
    glUniform3fv(locObjCol,   1, glm::value_ptr(renderObj->color));
    glUniform3fv(locLightCol, 1, glm::value_ptr(lightColor));

    glBindVertexArray(renderObj->vao);
    glDrawArrays(GL_TRIANGLES, 0, renderObj->vertex_count);
}

static void delete_object(RenderObj renderObj){
    glDeleteProgram(renderObj.prog);
    glDeleteBuffers(1, &renderObj.vbo);
    glDeleteVertexArrays(1, &renderObj.vao);
}

static void create_scene(Scene* scene){
    scene->prog = createProgram("assets/shaders/lit_shader.vs", "assets/shaders/lit_shader.fs");
    scene->selected = 0;
    orbitcamera_initialize(&scene->orbitCamera);
    create_render_object(
        scene,
        "assets/models/Planet.obj",
        glm::vec3(-1.0,0.0,0.0),
        glm::vec3(0.0, 0.0, 0.0),
        glm::vec3(0.2,0.2,0.2),
        glm::vec3(0.9f, 0.55f, 0.2f));

    create_render_object(
        scene,
        "assets/models/funnything.obj",
        glm::vec3(1.0,0.0,0.0),
        glm::vec3(0.0, 0.0, 0.0),
        glm::vec3(0.2,0.2,0.2),
        glm::vec3(0.2f, 0.55f, 0.9f));

    create_render_object(
        scene,
        "assets/models/buildings.obj",
        glm::vec3(0.0,-0.6,0.0),
        glm::vec3(0.0, 0.0, 0.0),
        glm::vec3(0.2,0.2,0.2),
        glm::vec3(0.2f, 0.9f, 0.2f));

    scene->lightPos = glm::vec3(1.2f, 1.5f, 1.0f);
}

static void delete_scene(Scene* scene){
    for(int i = 0;i < scene->renderObjs.size(); i++){
        delete_object(scene->renderObjs[i]);
    }
}

static void CreateOrResizeSceneFBO(SceneFBO *s, int w, int h)
{
    if (w <= 0 || h <= 0) return;

    // if same size and already created, do nothing
    if (s->fbo != 0 && s->w == w && s->h == h) return;

    // destroy old
    if (s->depth) glDeleteRenderbuffers(1, &s->depth);
    if (s->color) glDeleteTextures(1, &s->color);
    if (s->fbo)   glDeleteFramebuffers(1, &s->fbo);

    s->w = w; s->h = h;

    glGenFramebuffers(1, &s->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);

    // color texture
    glGenTextures(1, &s->color);
    glBindTexture(GL_TEXTURE_2D, s->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->color, 0);

    // depth buffer
    glGenRenderbuffers(1, &s->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s->depth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Scene FBO incomplete: " << status << "\n";
    }
}

static void RenderSceneToFBO(SceneFBO *s, Scene *scene)
{
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glViewport(0, 0, s->w, s->h);

    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, s->w, s->h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec3 camPos = orbitcamera_position(&scene->orbitCamera);
    glm::mat4 view = orbitcamera_view(&scene->orbitCamera);
    glm::mat4 proj = orbitcamera_proj(&scene->orbitCamera, (float)s->w / (float)s->h);
    for(int i = 0; i < scene->renderObjs.size(); i++){
        render_object(&scene->renderObjs[i], view, proj, scene->animLight, camPos);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void InitImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable docking + multi-viewport (requires docking branch or new enough ImGui)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

static void destroyImGui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

static void RenderImGuiFrame(GLFWwindow* window, Scene *scene, SceneFBO *s)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1) Create a full-screen dockspace host window
    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("DockSpaceHost", nullptr, host_flags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    ImGui::Begin("Inspector");
    RenderObj *o = &scene->renderObjs[scene->selected];
    ImGui::DragFloat3("position", &o->position.x, 0.01f);
    ImGui::DragFloat3("rotation", &o->rotation.x, 1.0);
    ImGui::DragFloat3("scale", &o->scale.x, 0.01f);
    ImGui::ColorEdit3("color", &o->color.x);
    ImGui::End();

    ImGui::Begin("Hierarchy");
    for(int i=0;i<scene->renderObjs.size();i++){
        if(ImGui::Button(scene->renderObjs[i].name.c_str())){
            scene->selected = i;
        }
    }
    ImGui::End();

    ImGui::Begin("Scene");

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x;
    int h = (int)avail.y;

    CreateOrResizeSceneFBO(s, w, h);
    RenderSceneToFBO(s, scene);

    ImGui::Image((ImTextureID)(intptr_t)s->color, avail, ImVec2(0, 1), ImVec2(1, 0));

    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetGizmoSizeClipSpace(0.2f);
    ImGuizmo::SetRect(
        ImGui::GetWindowPos().x,
        ImGui::GetWindowPos().y,
        ImGui::GetWindowWidth(),
        ImGui::GetWindowHeight()
    );

    glm::mat4 view = orbitcamera_view(&scene->orbitCamera);
    glm::mat4 proj = orbitcamera_proj(&scene->orbitCamera, (float)s->w / (float)s->h);
    glm::mat4 model = renderobject_model(&scene->renderObjs[scene->selected]);

    if(ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        ImGuizmo::TRANSLATE,
        ImGuizmo::LOCAL,
        glm::value_ptr(model)
    )){
        scene->renderObjs[scene->selected].position = glm::vec3(model[3]);
    }

    ImGui::End();

    // Render
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    auto io = ImGui::GetIO();
    // Multi-viewport support (ONLY if enabled)
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

}

double lastXPos = 0, lastYPos = 0;
int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  // Modern core context
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Models", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to init GLAD\n";
    return 1;
  }

  std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";

  glEnable(GL_DEPTH_TEST);

  SceneFBO s;
  CreateOrResizeSceneFBO(&s, 1000, 800);
  Scene scene;
  create_scene(&scene);

  glUseProgram(scene.prog);

  InitImGui(window);
  float rotation = 0;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    float t = (float)glfwGetTime();

    if(glfwGetKey(window, GLFW_KEY_LEFT)){
        rotation -= 0.01;
    }
    if(glfwGetKey(window, GLFW_KEY_RIGHT)){
        rotation += 0.01;
    }
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
    {
        orbitcamera_rotate(&scene.orbitCamera, xpos - lastXPos, -(ypos - lastYPos));
    }
    if(glfwGetKey(window, GLFW_KEY_EQUAL)){
        orbitcamera_zoom(&scene.orbitCamera, 0.1);
    }
    if(glfwGetKey(window, GLFW_KEY_MINUS)){
        orbitcamera_zoom(&scene.orbitCamera, -0.1);
    }
    float aspect = (s.h == 0) ? 1.0f : (float)s.w / (float)s.h;
    scene.animLight = scene.lightPos + glm::vec3(std::cos(t) * 0.4f, 0.0f, std::sin(t) * 0.4f);

    RenderImGuiFrame(window, &scene, &s);
    lastXPos = xpos;
    lastYPos = ypos;
    glfwSwapBuffers(window);
  }
  delete_scene(&scene);
  destroyImGui();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
