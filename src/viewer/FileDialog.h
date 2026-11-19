#pragma once
#include <string>
struct GLFWwindow;
// UTF-8 path, empty on cancellation; other failures throw.
std::string selectUsdFile(GLFWwindow *owner);
