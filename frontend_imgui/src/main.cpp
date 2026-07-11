#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <utility>

#include "app.hpp"
#include "font_manager.hpp"
#include "theme.hpp"
#include "zedit/core/config.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

namespace {

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

zedit::core::Editor make_editor(int argc, char** argv) {
  if (argc < 2) {
    return zedit::core::Editor();
  }
  try {
    return zedit::core::Editor::open_file(argv[1]);
  } catch (const zedit::core::FileIoError& e) {
    std::fprintf(stderr, "zedit: %s (starting with an empty buffer)\n",
                 e.what());
    zedit::core::Editor ed;
    ed.set_filename(argv[1]);
    return ed;
  }
}

// Loaded once at startup, applied to the editor and the frontend theme.
// Lua errors are printed but never abort startup -- a broken config
// shouldn't leave the user unable to open the editor to go fix it.
void apply_config(zedit::core::Editor& editor) {
  zedit::core::ConfigResult config =
      zedit::core::load_config_file(zedit::core::default_config_path());
  if (config.options.tabstop.has_value()) {
    editor.set_tabstop(*config.options.tabstop);
  }
  editor.set_normal_remap(config.normal_remap);
  zedit::frontend::apply_color_overrides(config.colors);
  for (const std::string& err : config.errors) {
    std::fprintf(stderr, "zedit: config error: %s\n", err.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    return 1;
  }

  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  GLFWwindow* window = glfwCreateWindow(1280, 800, "zedit", nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImFont* font = zedit::frontend::load_monospace_font(io, 18.0f);

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  zedit::core::Editor editor = make_editor(argc, argv);
  apply_config(editor);
  zedit::frontend::App app(std::move(editor), font);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    app.render_frame(io);
    if (app.should_close()) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    ImGui::Render();
    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
