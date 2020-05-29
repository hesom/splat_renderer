#define _USE_MATH_DEFINES

#include <iostream>
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>
#include <memory>
#include <string>
#include <fstream>
#include <vector>

#include <math.h>

#include "camera.h"

const int WIDTH = 1280;
const int HEIGHT = 720;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);
GLenum glCheckError_(const char *file, int line);
#define glCheckError() glCheckError_(__FILE__, __LINE__) 

Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
float lastX = (float)WIDTH / 2.0;
float lastY = (float)HEIGHT / 2.0;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

GLuint vao;
GLuint vbo;
GLuint instanceVbo;
GLuint program;
int num_points;

std::vector<float> positions = {
    0.0f, 0.0f, -1.0f,
    0.0f, -0.5f, -1.0f,
    0.0f, 0.5f, -1.0f,
    -0.5f, 0.0f, -1.0f,
    0.5f, 0.0f, -1.0f,
    -0.5f, -0.5f, -1.0f,
    0.5f, 0.5f, -1.0f,
    -0.5f, 0.5f, -1.0f,
    0.5f, -0.5f, -1.0f,
};

std::string readFromFile(const std::string &path);
void writeMat(const glm::mat4 &mat);

void initBuffers();
void initShaders();
bool checkShader(GLuint shaderId, GLuint type);
bool checkProgram(GLuint program);

std::vector<float> buildCircle(int fans, float radius);

int main()
{
    GLFWwindow *window;

    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwSetErrorCallback([](int error, const char *description) {
        fprintf(stderr, "Error: %s\n", description);
    });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Splat Renderer", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    initBuffers();
    initShaders();

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // input
        processInput(window);

        // render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto projection = glm::perspective(glm::radians(90.0f), (float)width / (float)height, 0.1f, 1000.0f);
        auto view = camera.GetViewMatrix();

        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection[0]));
        glUniformMatrix4fv(glGetUniformLocation(program, "view"), 1, GL_FALSE, glm::value_ptr(view[0]));
        glCheckError();

        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, num_points, positions.size() / 3);

        // swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    return 0;
}

std::string readFromFile(const std::string &path)
{
    std::string content;
    std::ifstream fileStream(path, std::ios::in);

    if (!fileStream.is_open())
    {
        throw std::runtime_error("Could not load shader " + path);
    }

    std::string line = "";
    while (!fileStream.eof())
    {
        std::getline(fileStream, line);
        content.append(line + "\n");
    }

    fileStream.close();
    return content;
}

void initBuffers()
{

    auto circle = buildCircle(100, 0.1f);

    num_points = circle.size();

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, circle.size() * sizeof(float), circle.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1);
    glBindVertexArray(0);
}

void initShaders()
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    auto vertexSource = readFromFile("src/shaders/splat.vert");
    const char *vertexSource_c = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexSource_c, nullptr);
    glCompileShader(vertexShader);
    if (!checkShader(vertexShader, GL_VERTEX_SHADER))
    {
        throw std::runtime_error("Shader compilation failed");
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    auto fragmentSource = readFromFile("src/shaders/splat.frag");
    const char *fragmentSource_c = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSource_c, nullptr);
    glCompileShader(fragmentShader);
    if (!checkShader(fragmentShader, GL_FRAGMENT_SHADER))
    {
        throw std::runtime_error("Shader compilation failed");
    }

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    if (!checkProgram(program))
    {
        throw std::runtime_error("Shader linking failed");
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

bool checkShader(GLuint shaderId, GLuint type)
{
    int success;
    char infoLog[512];
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);

    std::string typeString = type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT";

    if (!success)
    {
        glGetShaderInfoLog(shaderId, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::" << typeString << "::COMPILATION::FAILED\n"
                  << infoLog << std::endl;
        return false;
    }

    return true;
}

bool checkProgram(GLuint program)
{
    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success)
    {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cout << "ERROR::PROGRAM::LINKING::FAILED\n"
                  << infoLog << std::endl;
        return false;
    }

    return true;
}

std::vector<float> buildCircle(int fans, float radius)
{
    std::vector<float> vertices{0.0f, 0.0f, 0.0f};

    for (int i = 0; i <= fans; i++)
    {
        float x = radius * static_cast<float>(cos((static_cast<float>(i) / static_cast<float>(fans)) * 2.0 * M_PI));
        float y = radius * static_cast<float>(sin((static_cast<float>(i) / static_cast<float>(fans)) * 2.0 * M_PI));

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);
    }

    return vertices;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(yoffset);
}

GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR)
    {
        std::string error;
        switch (errorCode)
        {
        case GL_INVALID_ENUM:
            error = "INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            error = "INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            error = "INVALID_OPERATION";
            break;
        case GL_STACK_OVERFLOW:
            error = "STACK_OVERFLOW";
            break;
        case GL_STACK_UNDERFLOW:
            error = "STACK_UNDERFLOW";
            break;
        case GL_OUT_OF_MEMORY:
            error = "OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            error = "INVALID_FRAMEBUFFER_OPERATION";
            break;
        }
        std::cout << error << " | " << file << " (" << line << ")" << std::endl;
    }
    return errorCode;
}

void writeMat(const glm::mat4 &mat)
{
    std::cout << mat[0][0] << "," << mat[0][1] << "," << mat[0][2] << "," << mat[0][3] << std::endl;
    std::cout << mat[1][0] << "," << mat[1][1] << "," << mat[1][2] << "," << mat[1][3] << std::endl;
    std::cout << mat[2][0] << "," << mat[2][1] << "," << mat[2][2] << "," << mat[2][3] << std::endl;
    std::cout << mat[3][0] << "," << mat[3][1] << "," << mat[3][2] << "," << mat[3][3] << std::endl;
    std::cout << std::endl;
}
