#define _USE_MATH_DEFINES

#include <iostream>
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <tinyply.h>

#include <stdexcept>
#include <memory>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <future>
#include "utils.h"

#include <math.h>

#include "camera.h"

// Camera params for projection
const int WIDTH = 640;
const int HEIGHT = 480;

const float FX = 528.0f;
const float FY = 528.0f;
const float CX = 320.0f;
const float CY = 240.0f;

// Near and far clipping planes in m
const float NEAR = 0.01f;
const float FAR = 8.0f;

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);
GLenum glCheckError_(const char *file, int line);
#define glCheckError() glCheckError_(__FILE__, __LINE__)

Camera camera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
double lastX = static_cast<double>(WIDTH) / 2.0;
double lastY = static_cast<double>(HEIGHT) / 2.0;
bool firstMouse = true;
double deltaTime = 0.0f;
double lastFrame = 0.0f;
int frame = 0;

GLuint vao;
GLuint vbo;
GLuint instanceVbo;
GLuint radiusVbo;
GLuint normalVbo;
GLuint colorVbo;
GLuint program;
size_t num_points;

struct PointCloud
{
    std::vector<float3> position;
    std::vector<uchar3> color;
    std::vector<float3> normal;
    std::vector<float> confidence;
    std::vector<float> radius;
    size_t size;
};

std::string readFromFile(const std::string &path);
void writeMat(const glm::mat4 &mat);
PointCloud readPly(const std::string &filepath);

void initBuffers(const PointCloud &pcl);
void initShaders();
bool checkShader(GLuint shaderId, GLuint type);
bool checkProgram(GLuint program);

std::vector<float> buildCircle(int fans, float radius);
std::vector<glm::mat4> loadTrajectoryFromFile(std::string path);

using namespace tinyply;

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

    auto pcl = readPly("models/scene0_anim2.ply");
    auto trajectory = loadTrajectoryFromFile("models/scene0_anim2.freiburg");

    std::cout << "Max radius" << *std::max_element(pcl.radius.begin(), pcl.radius.end()) << std::endl;
    std::cout << "Min radius" << *std::min_element(pcl.radius.begin(), pcl.radius.end()) << std::endl;

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    initBuffers(pcl);
    initShaders();

    for (const auto &view : trajectory)
    {
        double currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // input
        processInput(window);
        // render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 m(0.0f);
        m[0][0] = 2.0f * FX / WIDTH;
        m[0][1] = 0.0f;
        m[0][2] = 0.0f;
        m[0][3] = 0.0f;

        m[1][0] = 0.0f;
        m[1][1] = -2.0f * FY / HEIGHT;
        m[1][2] = 0.0f;
        m[1][3] = 0.0f;

        m[2][0] = 1.0f - 2.0f * CX / WIDTH;
        m[2][1] = 2.0f * CY / HEIGHT - 1.0f;
        m[2][2] = (FAR + NEAR) / (NEAR - FAR);
        m[2][3] = -1.0f;

        m[3][0] = 0.0f;
        m[3][1] = 0.0f;
        m[3][2] = 2.0f * FAR * NEAR / (NEAR - FAR);
        m[3][3] = 0.0f;

        auto projection = m;
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection[0]));
        glUniformMatrix4fv(glGetUniformLocation(program, "view"), 1, GL_FALSE, glm::value_ptr(view[0]));

        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, num_points, pcl.size);

        // swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &instanceVbo);
    glDeleteBuffers(1, &radiusVbo);
    glDeleteBuffers(1, &colorVbo);
    glDeleteBuffers(1, &normalVbo);
    glDeleteVertexArrays(1, &vao);

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

void initBuffers(const PointCloud &pcl)
{

    auto circle = buildCircle(100, 1.0f);

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
    glBufferData(GL_ARRAY_BUFFER, pcl.position.size() * sizeof(float3), pcl.position.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1);

    glGenBuffers(1, &radiusVbo);
    glBindBuffer(GL_ARRAY_BUFFER, radiusVbo);
    glBufferData(GL_ARRAY_BUFFER, pcl.radius.size() * sizeof(float), pcl.radius.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void *)0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    glGenBuffers(1, &colorVbo);
    glBindBuffer(GL_ARRAY_BUFFER, colorVbo);
    glBufferData(GL_ARRAY_BUFFER, pcl.color.size() * sizeof(uchar3), pcl.color.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(3, 3, GL_UNSIGNED_BYTE, GL_TRUE, 3 * sizeof(unsigned char), (void *)0);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    glGenBuffers(1, &normalVbo);
    glBindBuffer(GL_ARRAY_BUFFER, normalVbo);
    glBufferData(GL_ARRAY_BUFFER, pcl.normal.size() * sizeof(float3), pcl.normal.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(4, 3, GL_FLOAT, GL_TRUE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

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
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS)
        frame = (frame + 1) % 499;
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS)
        frame = frame == 0 ? 498 : frame - 1;
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

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

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

PointCloud readPly(const std::string &filepath)
{
    std::unique_ptr<std::istream> file_stream;
    std::vector<uint8_t> byte_buffer;
    try
    {
        byte_buffer = read_file_binary(filepath);
        file_stream.reset(new memory_stream((char *)byte_buffer.data(), byte_buffer.size()));

        if (!file_stream || file_stream->fail())
            throw std::runtime_error("file_stream failed to open " + filepath);

        file_stream->seekg(0, std::ios::end);
        const float size_mb = file_stream->tellg() * float(1e-6);
        file_stream->seekg(0, std::ios::beg);

        PlyFile file;
        file.parse_header(*file_stream);

        std::cout << "\t[ply_header] Type: " << (file.is_binary_file() ? "binary" : "ascii") << std::endl;
        for (const auto &c : file.get_comments())
            std::cout << "\t[ply_header] Comment: " << c << std::endl;
        for (const auto &c : file.get_info())
            std::cout << "\t[ply_header] Info: " << c << std::endl;

        for (const auto &e : file.get_elements())
        {
            std::cout << "\t[ply_header] element: " << e.name << " (" << e.size << ")" << std::endl;
            for (const auto &p : e.properties)
            {
                std::cout << "\t[ply_header] \tproperty: " << p.name << " (type=" << tinyply::PropertyTable[p.propertyType].str << ")";
                if (p.isList)
                    std::cout << " (list_type=" << tinyply::PropertyTable[p.listType].str << ")";
                std::cout << std::endl;
            }
        }

        std::shared_ptr<PlyData> position, normal, color, confidence, radius;

        try
        {
            position = file.request_properties_from_element("vertex", {"x", "y", "z"});
        }
        catch (const std::exception &e)
        {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        {
            normal = file.request_properties_from_element("vertex", {"nx", "ny", "nz"});
        }
        catch (const std::exception &e)
        {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        {
            color = file.request_properties_from_element("vertex", {"red", "green", "blue"});
        }
        catch (const std::exception &e)
        {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        {
            confidence = file.request_properties_from_element("vertex", {"confidence"});
        }
        catch (const std::exception &e)
        {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        {
            radius = file.request_properties_from_element("vertex", {"radius"});
        }
        catch (const std::exception &e)
        {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        manual_timer read_timer;

        read_timer.start();
        file.read(*file_stream);
        read_timer.stop();

        const double parsing_time = read_timer.get() / 1000.f;
        std::cout << "\tparsing " << size_mb << "mb in " << parsing_time << " seconds [" << (size_mb / parsing_time) << " MBps]" << std::endl;

        if (position)
            std::cout << "\tRead " << position->count << " total vertices " << std::endl;
        if (normal)
            std::cout << "\tRead " << normal->count << " total vertex normals " << std::endl;
        if (color)
            std::cout << "\tRead " << color->count << " total vertex colors " << std::endl;
        if (confidence)
            std::cout << "\tRead " << confidence->count << " total confidence counters " << std::endl;
        if (radius)
            std::cout << "\tRead " << radius->count << " total point radii " << std::endl;

        PointCloud pcl;

        pcl.position.resize(position->count);
        pcl.normal.resize(normal->count);
        pcl.color.resize(color->count);
        pcl.confidence.resize(confidence->count);
        pcl.radius.resize(radius->count);

        auto t1 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = position->buffer.size_bytes();
            std::memcpy(pcl.position.data(), position->buffer.get(), numVerticesBytes);
            std::transform(pcl.position.begin(), pcl.position.end(), pcl.position.begin(), [](float3 position) {
                return float3{-position.x, position.y, position.z};
            });
        });

        auto t2 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = normal->buffer.size_bytes();
            std::memcpy(pcl.normal.data(), normal->buffer.get(), numVerticesBytes);
            std::transform(pcl.normal.begin(), pcl.normal.end(), pcl.normal.begin(), [](float3 normal) {
                return float3{-normal.x, normal.y, normal.z};
            });
        });

        auto t3 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = color->buffer.size_bytes();
            std::memcpy(pcl.color.data(), color->buffer.get(), numVerticesBytes);
        });

        auto t4 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = confidence->buffer.size_bytes();
            std::memcpy(pcl.confidence.data(), confidence->buffer.get(), numVerticesBytes);
        });

        auto t5 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = radius->buffer.size_bytes();
            std::memcpy(pcl.radius.data(), radius->buffer.get(), numVerticesBytes);
        });

        t1.get();
        t2.get();
        t3.get();
        t4.get();
        t5.get();

        pcl.size = pcl.position.size();
        return pcl;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        exit(1);
    }
}

std::vector<glm::mat4> loadTrajectoryFromFile(std::string path)
{
    std::vector<glm::mat4> trajectory;
    std::ifstream ifs;
    float timestamp, tx, ty, tz, qx, qy, qz, qw;

    ifs.open(path);
    if (!ifs)
    {
        throw std::runtime_error("Unable to open trajectory file");
        exit(1);
    }

    // transform the trajectory into the right basis for our application
    auto coordinateTransform = glm::mat4({-1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, -1.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 1.0f});

    while (ifs >> timestamp >> tx >> ty >> tz >> qx >> qy >> qz >> qw)
    {
        auto rotationQuat = glm::quat(qw, -qx, -qy, qz);
        auto rotationMat = glm::transpose(glm::toMat4(rotationQuat));
        auto translationMat = glm::translate(glm::mat4(1.0f), -glm::vec3(tx, ty, -tz));
        auto transform = rotationMat * translationMat;
        trajectory.push_back(transform * coordinateTransform);
    }

    return trajectory;
}