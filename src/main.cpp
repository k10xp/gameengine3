#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <type_traits>

static std::vector<float> load_obj(const std::string& path)
{
    std::vector<float> out;   // flat list: px py pz nx ny nz
    std::vector<std::array<float, 3>> verts;
    std::vector<std::array<float, 3>> norms;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ file: " << path << std::endl;
        return out;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v") {
            float x, y, z;
            iss >> x >> y >> z;
            verts.push_back({x, y, z});
        }
        else if (type == "vn") {
            float x, y, z;
            iss >> x >> y >> z;
            norms.push_back({x, y, z});
        }
        else if (type == "f") {
            std::vector<std::string> face;
            std::string tok;
            while (iss >> tok)
                face.push_back(tok);

            if (face.size() < 3)
                continue;

            auto parse_tok = [](const std::string& t) {
                int vi = -1;
                int ni = -1;

                std::stringstream ss(t);
                std::string part;

                std::getline(ss, part, '/');
                vi = std::stoi(part) - 1;

                // skip vt
                if (std::getline(ss, part, '/')) {
                    if (std::getline(ss, part, '/')) {
                        if (!part.empty())
                            ni = std::stoi(part) - 1;
                    }
                }
                return std::pair<int, int>(vi, ni);
            };

            auto [v0i, n0i] = parse_tok(face[0]);

            // triangulate using fan method
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                auto [v1i, n1i] = parse_tok(face[i]);
                auto [v2i, n2i] = parse_tok(face[i + 1]);

                int vis[3] = {v0i, v1i, v2i};
                int nis[3] = {n0i, n1i, n2i};

                for (int k = 0; k < 3; ++k) {
                    const auto& p = verts[vis[k]];
                    float nx = 0.f, ny = 0.f, nz = 0.f;

                    if (nis[k] >= 0 && nis[k] < (int)norms.size()) {
                        const auto& n = norms[nis[k]];
                        nx = n[0];
                        ny = n[1];
                        nz = n[2];
                    }

                    out.push_back(p[0]);
                    out.push_back(p[1]);
                    out.push_back(p[2]);
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

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
  glViewport(0, 0, w, h);
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
    GLuint prog;
    GLuint vao, vbo;
    int vertex_count;
    glm::mat4 model;
    glm::vec3 objectColor;
};

static RenderObj create_render_object(GLuint prog, std::string modelPath){
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
    renderObj.prog = prog;
    renderObj.vao = vao;
    renderObj.vbo = vbo;
    renderObj.vertex_count = vertices.size() / 6;
    return renderObj;
}

static void render_object(RenderObj renderObj, glm::mat4 view, glm::mat4 proj, glm::vec3 lightPos, glm::vec3 camPos){
    glUseProgram(renderObj.prog);
    const GLint locModel = glGetUniformLocation(renderObj.prog, "uModel");
    const GLint locView  = glGetUniformLocation(renderObj.prog, "uView");
    const GLint locProj  = glGetUniformLocation(renderObj.prog, "uProj");
    const GLint locLightPos = glGetUniformLocation(renderObj.prog, "uLightPos");
    const GLint locViewPos  = glGetUniformLocation(renderObj.prog, "uViewPos");
    const GLint locObjCol   = glGetUniformLocation(renderObj.prog, "uObjectColor");
    const GLint locLightCol = glGetUniformLocation(renderObj.prog, "uLightColor");

    glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(renderObj.model));
    glUniformMatrix4fv(locView,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(locProj,  1, GL_FALSE, glm::value_ptr(proj));

    glUniform3fv(locLightPos, 1, glm::value_ptr(lightPos));
    glUniform3fv(locViewPos,  1, glm::value_ptr(camPos));

    glm::vec3 lightColor (1.0f, 1.0f, 1.0f);
    glUniform3fv(locObjCol,   1, glm::value_ptr(renderObj.objectColor));
    glUniform3fv(locLightCol, 1, glm::value_ptr(lightColor));

    glBindVertexArray(renderObj.vao);
    glDrawArrays(GL_TRIANGLES, 0, renderObj.vertex_count);
}

static void delete_object(RenderObj renderObj){
    glDeleteProgram(renderObj.prog);
    glDeleteBuffers(1, &renderObj.vbo);
    glDeleteVertexArrays(1, &renderObj.vao);
}

static glm::mat4 trs(glm::vec3 position, float angleRadians, glm::vec3 scale){
    glm::mat4 matrix = glm::mat4(1.0f);
    matrix = glm::translate(matrix, position);
    matrix = glm::rotate(matrix, angleRadians, glm::vec3(0.0,1.0,0.0));
    matrix = glm::scale(matrix, scale);
    return matrix;
}

int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  // Modern core context
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* win = glfwCreateWindow(1280, 720, "Models", nullptr, nullptr);
  if (!win) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);
  glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to init GLAD\n";
    return 1;
  }

  std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";

  glEnable(GL_DEPTH_TEST);

  GLuint prog = createProgram("assets/shaders/lit_shader.vs", "assets/shaders/lit_shader.fs");
  RenderObj icoSphere = create_render_object(prog, "assets/models/Planet.obj");
  RenderObj funnyThing = create_render_object(prog, "assets/models/funnything.obj");
  RenderObj buildings = create_render_object(prog, "assets/models/buildings.obj");

  glUseProgram(prog);

  // Basic “camera”
  glm::vec3 camTarget = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 camUp = glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 lightPos = glm::vec3(1.2f, 1.5f, 1.0f);

  while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(win, GLFW_TRUE);

    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    float aspect = (h == 0) ? 1.0f : (float)w / (float)h;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Animate a little
    float t = (float)glfwGetTime();
    glm::vec3 camPos   = glm::vec3(cos(t)*3, 2.2f, sin(t)*3);

    icoSphere.model = trs(glm::vec3(-1.0,0.0,0.0), 0, glm::vec3(0.2,0.2,0.2));
    icoSphere.objectColor = glm::vec3(0.9f, 0.55f, 0.2f);

    funnyThing.model = trs(glm::vec3(1.0,0.0,0.0), 0, glm::vec3(0.2,0.2,0.2));
    funnyThing.objectColor = glm::vec3(0.2f, 0.55f, 0.9f);

    buildings.model = trs(glm::vec3(0.0,-0.6,0.0), 0, glm::vec3(0.2,0.2,0.2));
    buildings.objectColor = glm::vec3(0.2f, 0.9f, 0.2f);

    glm::mat4 view = glm::lookAt(camPos, camTarget, camUp);
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
    glm::vec3 animLight = lightPos + glm::vec3(std::cos(t) * 0.4f, 0.0f, std::sin(t) * 0.4f);

    render_object(icoSphere, view, proj, animLight, camPos);
    render_object(funnyThing, view, proj, animLight, camPos);
    render_object(buildings, view, proj, animLight, camPos);

    glfwSwapBuffers(win);
  }
  delete_object(icoSphere);

  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}
