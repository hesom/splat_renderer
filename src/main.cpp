#define _USE_MATH_DEFINES

#include <iostream>
#include <iomanip>
#include <sstream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
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
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>
#include <pybind11/pybind11.h>

#include <math.h>

#include "camera.h"
#include "utils.h"
#include "lodepng.h"
#include "splat.vert.h"
#include "ewasplat.vert.h"
#include "splat.frag.h"
#include "visibility.frag.h"
#include "visibility.vert.h"
#include "splatcount.frag.h"
#include "final.frag.h"
#include "fullscreenquad.vert.h"

// Near and far clipping planes in m
const float NEAR = 0.01f;
const float FAR = 12.0f;

GLenum glCheckError_(const char *file, int line);
#define glCheckError() glCheckError_(__FILE__, __LINE__)

const size_t NUM_PBOS = 3;  //triple buffering

GLuint vao;
GLuint vbo;
GLuint instanceVbo;
GLuint radiusVbo;
GLuint normalVbo;
GLuint colorVbo;
GLuint colorPbos[NUM_PBOS];
GLuint depthPbos[NUM_PBOS];
GLuint program;

// EWA specific resources
GLuint visibilityPassProgram;
GLuint splatcountProgram;
GLuint finalPassProgram;
GLuint fbo;
GLuint quadVao;
GLuint quadVbo;
GLuint depthBuffer;
GLuint depthAccTexture;
GLuint colorAccTexture;
GLuint normalTexture;
GLuint counterTexture;

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
PointCloud readPly(const std::string &filepath, float defaultPointSize);

size_t initBuffers(const PointCloud &pcl, int width, int height);
void initShaders();
void initEWAShaders();
bool checkShader(GLuint shaderId, GLuint type);
bool checkProgram(GLuint program);
void initEWASpecificBuffers(int width, int height);

std::vector<float> buildCircle(int fans, float radius);
std::vector<glm::mat4> loadTrajectoryFromFile(std::string path);

using namespace tinyply;
namespace py = pybind11;
namespace fs = std::experimental::filesystem;

int render(std::string pointcloudPath, std::string trajectoryPath, std::string outputPath, int delta=1, float pointSize=1e-2f,
    int width = 640, int height=480, float fx=528.0f, float fy=528.0f, float cx=320.0f, float cy=240.0f,
    float depthScale=1000.0f, std::string method="standard", float surfaceThickness=0.1f)
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
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    window = glfwCreateWindow(width, height, "Splat Renderer", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    auto pcl = readPly(pointcloudPath, pointSize);
    auto trajectory = loadTrajectoryFromFile(trajectoryPath);
    std::vector<GLubyte*> rgbBuffers;
    std::vector<float*> depthBuffers;

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_DEPTH_TEST);
    //glEnable(GL_CULL_FACE);
    //glCullFace(GL_BACK);

    const auto pointsPerCircle = initBuffers(pcl, width, height);
    if(method=="ewa" || method=="EWA")
    {
        initEWASpecificBuffers(width, height);
        initEWAShaders();
    }else{
        initShaders();
    }

    size_t numDownloads = 0;
    size_t dx = 0;
    for (int frame = 0; frame<trajectory.size(); frame+=delta)
    {
        if(glfwWindowShouldClose(window)){
            glfwTerminate();
            return -1;
        }
        // render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 m(0.0f);
        m[0][0] = 2.0f * fx / width;
        m[0][1] = 0.0f;
        m[0][2] = 0.0f;
        m[0][3] = 0.0f;

        m[1][0] = 0.0f;
        m[1][1] = -2.0f * fy / height;
        m[1][2] = 0.0f;
        m[1][3] = 0.0f;

        m[2][0] = 1.0f - 2.0f * cx / width;
        m[2][1] = 2.0f * cy / height - 1.0f;
        m[2][2] = (FAR + NEAR) / (NEAR - FAR);
        m[2][3] = -1.0f;

        m[3][0] = 0.0f;
        m[3][1] = 0.0f;
        m[3][2] = 2.0f * FAR * NEAR / (NEAR - FAR);
        m[3][3] = 0.0f;

        auto projection = m;
        auto view = trajectory.at(frame);

        if(method=="ewa" || method=="EWA")
        {   
            // VISIBILITY PASS
            {
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glUseProgram(visibilityPassProgram);
                auto modelviewLoc = glGetUniformLocation(visibilityPassProgram, "modelview");
                glUniformMatrix4fv(modelviewLoc, 1, GL_FALSE, glm::value_ptr(view[0]));
                auto projectionLoc = glGetUniformLocation(visibilityPassProgram, "projection");
                glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection[0]));
                auto epsilonLoc = glGetUniformLocation(visibilityPassProgram, "epsilon");
                glUniform1f(epsilonLoc, surfaceThickness);

                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glBindVertexArray(vao);
                glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, pointsPerCircle, pcl.size);
                glCheckError();
            }

            // ACCUMULATION PASS
            {
                glUseProgram(splatcountProgram);
                auto modelviewLoc = glGetUniformLocation(splatcountProgram, "modelview");
                glUniformMatrix4fv(modelviewLoc, 1, GL_FALSE, glm::value_ptr(view[0]));
                auto projectionLoc = glGetUniformLocation(splatcountProgram, "projection");
                glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection[0]));
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_MIN);
                glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
                GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
                glDrawBuffers(3, drawBuffers);
                glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, pointsPerCircle, pcl.size);
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glCheckError();
            }

            // INTERPOLATION PASS
            {
                glUseProgram(finalPassProgram);
                auto nearLoc = glGetUniformLocation(finalPassProgram, "near");
                glUniform1f(nearLoc, NEAR);
                auto farLoc = glGetUniformLocation(finalPassProgram, "far");
                glUniform1f(farLoc, FAR);
                auto colorLoc = glGetUniformLocation(finalPassProgram, "colorAccTexture");
                glUniform1i(colorLoc, 0);
                auto depthLoc = glGetUniformLocation(finalPassProgram, "depthAccTexture");
                glUniform1i(depthLoc, 1);
                auto counterLoc = glGetUniformLocation(finalPassProgram, "counterTexture");
                glUniform1i(counterLoc, 2);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glDisable(GL_CULL_FACE);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, colorAccTexture);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, depthAccTexture);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, counterTexture);

                glBindVertexArray(quadVao);
                glDepthFunc(GL_ALWAYS); // enables us to still write to depth buffer
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glCheckError();
                glDepthFunc(GL_LESS);
                glDisable(GL_DEPTH_TEST);
            }
        }else{
            glUseProgram(program);
            glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection[0]));
            glUniformMatrix4fv(glGetUniformLocation(program, "view"), 1, GL_FALSE, glm::value_ptr(view[0]));

            glBindVertexArray(vao);
            glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, pointsPerCircle, pcl.size);
        }
        if (numDownloads < NUM_PBOS)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, colorPbos[dx]);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, depthPbos[dx]);
            glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
        }
        else
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, colorPbos[dx]);
            GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr)
            {
                GLubyte* cpuBuffer = new GLubyte[3*width*height];
                memcpy(cpuBuffer, ptr, 3 * width * height);
                rgbBuffers.push_back(cpuBuffer);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            else
            {
                std::cerr << "Could not map color PBO" << std::endl;
            }
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, depthPbos[dx]);
            float* ptr2 = (float*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr2)
            {
                float* cpuBuffer = new float[width * height];
                memcpy(cpuBuffer, ptr2, width * height*sizeof(float));
                depthBuffers.push_back(cpuBuffer);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            else
            {
                std::cerr << "Could not map depth PBO" << std::endl;
            }
            glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
        }

        dx = (dx + 1) % NUM_PBOS;
        numDownloads++;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        // swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // read remaining pbos
    for(int pbo = 0; pbo < NUM_PBOS; pbo++)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, colorPbos[dx]);
        GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr)
        {
            GLubyte* cpuBuffer = new GLubyte[3*width*height];
            memcpy(cpuBuffer, ptr, 3 * width * height);
            rgbBuffers.push_back(cpuBuffer);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        else
        {
            std::cerr << "Could not map color PBO" << std::endl;
        }
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, depthPbos[dx]);
        float* ptr2 = (float*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr2)
        {
            float* cpuBuffer = new float[width * height];
            memcpy(cpuBuffer, ptr2, width * height*sizeof(float));
            depthBuffers.push_back(cpuBuffer);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        else
        {
            std::cerr << "Could not map depth PBO" << std::endl;
        }
        dx = (dx + 1) % NUM_PBOS;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    
    for (int i = 0; i < rgbBuffers.size(); i++) {
        fs::create_directories(outputPath + "/debug");
        fs::create_directories(outputPath + "/depth");
        std::ostringstream ss;
        ss << std::setw(5) << std::setfill('0') << std::to_string(i*delta);
        auto fileNameColor = outputPath + "/debug/" + ss.str() + ".png";
        auto fileNameDepth = outputPath + "/depth/" + ss.str() + ".png";
        std::async(std::launch::async, [&]() {
            // flip image
            GLubyte* tmpColor = new GLubyte[width * height * 3];
            for (int row = 0; row < height; row++)
            {
                memcpy(&tmpColor[row * width * 3], &rgbBuffers.at(i)[(height - row-1) * width * 3], width*3);
            }
            lodepng::encode(fileNameColor, tmpColor, width, height, LCT_RGB, 8U);
            delete[] tmpColor;

            float* tmpDepth = new float[width * height];
            unsigned char* tmpDepthBytes = new unsigned char[width * height * 2];
            for (int row = 0; row < height; row++)
            {
                memcpy(&tmpDepth[row * width], &depthBuffers.at(i)[(height - row - 1) * width], width * sizeof(float));
            }
            for (int j = 0; j < width * height; j++)
            {
                float d = tmpDepth[j];
                if (d > 0.0f && d < 1.0f)
                {
                    d = (2.0f * d) - 1.0f;
                    d = (2.0f * NEAR * FAR) / (FAR + NEAR - (d*(FAR - NEAR)));
                }
                else
                {
                    d = 0.0f;
                }
                // d is not in meters so convert it to mm
                d *= depthScale;
                // we do not have sub milimeter accuracy so we can savely convert d to uint16_t
                uint16_t bytes = d;
                unsigned char lsb = bytes & 0xff;
                unsigned char msb = bytes >> 8;
                tmpDepthBytes[2*j + 0] = msb;
                tmpDepthBytes[2*j + 1] = lsb;
            }
            lodepng::encode(fileNameDepth, tmpDepthBytes, width, height, LCT_GREY, 16U);
            delete[] tmpDepth;
            delete[] tmpDepthBytes;
            }
        );
    }

    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &instanceVbo);
    glDeleteBuffers(1, &radiusVbo);
    glDeleteBuffers(1, &colorVbo);
    glDeleteBuffers(1, &normalVbo);
    glDeleteBuffers(NUM_PBOS, colorPbos);
    glDeleteBuffers(NUM_PBOS, depthPbos);
    glDeleteVertexArrays(1, &vao);

    if(method == "ewa" || method == "EWA"){
        glDeleteProgram(finalPassProgram);
        glDeleteProgram(visibilityPassProgram);

        glDeleteBuffers(1, &quadVbo);
        glDeleteVertexArrays(1, &quadVao);

        glDeleteTextures(1, &colorAccTexture);
        glDeleteTextures(1, &depthAccTexture);
        glDeleteTextures(1, &normalTexture);
        glDeleteTextures(1, &counterTexture);
        glDeleteRenderbuffers(1, &depthBuffer);
        glDeleteFramebuffers(1, &fbo);
    }

    glfwTerminate();

    for (auto& buffer : rgbBuffers)
    {
        delete[] buffer;
    }

    for (auto& buffer : depthBuffers)
    {
        delete[] buffer;
    }

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

size_t initBuffers(const PointCloud &pcl, int width, int height)
{
    auto circle = buildCircle(100, 1.0f);

    size_t num_points = circle.size();

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

    // Set up pbos for efficient pixel transfers
    glGenBuffers(NUM_PBOS, colorPbos);
    glGenBuffers(NUM_PBOS, depthPbos);
    int nbytes = width * height * 3;
    for (int i = 0; i < NUM_PBOS; i++)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, colorPbos[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, nbytes, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, depthPbos[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, width*height*sizeof(float), nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    return num_points;
}

void initEWASpecificBuffers(int width, int height)
{
    // Texture for depth accumulation pass
    glGenTextures(1, &depthAccTexture);
    glBindTexture(GL_TEXTURE_2D, depthAccTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // Texture for color accumulation pass
    glGenTextures(1, &colorAccTexture);
    glBindTexture(GL_TEXTURE_2D, colorAccTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // Counter texture for normalization
    glGenTextures(1, &counterTexture);
    glBindTexture(GL_TEXTURE_2D, counterTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorAccTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, counterTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, depthAccTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // quad for screen-space computations in fragment shader
    glGenVertexArrays(1, &quadVao);
    glBindVertexArray(quadVao);
    glGenBuffers(1, &quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    float quadVertices[] = {// positions   // texCoords
                            -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,

                            -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void initShaders()
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexSource = SPLAT_VERT_STR;
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);
    if (!checkShader(vertexShader, GL_VERTEX_SHADER))
    {
        throw std::runtime_error("Shader compilation failed");
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentSource = SPLAT_FRAG_STR;
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
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

    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void initEWAShaders()
{
    // -------- VISIBILITY PASS ------------
    {
        auto vertexShader = glCreateShader(GL_VERTEX_SHADER);
        auto vertexSourceStr = std::string(VISIBILITY_VERT_STR);
        const char *vertexSource = vertexSourceStr.c_str();
        glShaderSource(vertexShader, 1, &vertexSource, nullptr);
        glCompileShader(vertexShader);
        if (!checkShader(vertexShader, GL_VERTEX_SHADER))
        {
            throw std::runtime_error("Visibility vertex shader compilation failed");
        }

        auto fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        auto fragmentSourceStr = std::string(VISIBILITY_FRAG_STR);
        const char *fragmentSource = fragmentSourceStr.c_str();
        glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
        glCompileShader(fragmentShader);
        if (!checkShader(fragmentShader, GL_FRAGMENT_SHADER)) {
            throw std::runtime_error("Visibility fragment shader compilation failed");
        }

        visibilityPassProgram = glCreateProgram();
        glAttachShader(visibilityPassProgram, vertexShader);
        glAttachShader(visibilityPassProgram, fragmentShader);
        glLinkProgram(visibilityPassProgram);

        if(!checkProgram(visibilityPassProgram))
        {
            throw std::runtime_error("Visibility shader linking failed");
        }

        glDetachShader(visibilityPassProgram, vertexShader);
        glDetachShader(visibilityPassProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }
    // -----------------------------------------

    // ----------ACCUMULATION COUNT PASS--------------
    {
        auto vertexShader = glCreateShader(GL_VERTEX_SHADER);
        auto vertexSourceStr = std::string(EWASPLAT_VERT_STR);
        const char *vertexSource = vertexSourceStr.c_str();
        glShaderSource(vertexShader, 1, &vertexSource, nullptr);
        glCompileShader(vertexShader);
        if (!checkShader(vertexShader, GL_VERTEX_SHADER))
        {
            throw std::runtime_error("Accumulation vertex shader compilation failed");
        }

        auto fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        auto fragmentSourceStr = std::string(SPLATCOUNT_FRAG_STR);
        const char *fragmentSource = fragmentSourceStr.c_str();
        glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
        glCompileShader(fragmentShader);
        if (!checkShader(fragmentShader, GL_FRAGMENT_SHADER)) {
            throw std::runtime_error("Accumulation fragment shader compilation failed");
        }

        splatcountProgram = glCreateProgram();
        glAttachShader(splatcountProgram, vertexShader);
        glAttachShader(splatcountProgram, fragmentShader);
        glLinkProgram(splatcountProgram);

        if(!checkProgram(splatcountProgram))
        {
            throw std::runtime_error("Accumulation shader linking failed");
        }

        glDetachShader(splatcountProgram, vertexShader);
        glDetachShader(splatcountProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    // ----------INTERPOLATION PASS -----------------
    {
        auto vertexShader = glCreateShader(GL_VERTEX_SHADER);
        auto vertexSourceStr = std::string(FULLSCREENQUAD_VERT_STR);
        const char *vertexSource = vertexSourceStr.c_str();
        glShaderSource(vertexShader, 1, &vertexSource, nullptr);
        glCompileShader(vertexShader);
        if (!checkShader(vertexShader, GL_VERTEX_SHADER))
        {
            throw std::runtime_error("Interpolation vertex shader compilation failed");
        }

        auto fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        auto fragmentSourceStr = std::string(FINAL_FRAG_STR);
        const char *fragmentSource = fragmentSourceStr.c_str();
        glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
        glCompileShader(fragmentShader);
        if (!checkShader(fragmentShader, GL_FRAGMENT_SHADER)) {
            throw std::runtime_error("Interpolation fragment shader compilation failed");
        }

        finalPassProgram = glCreateProgram();
        glAttachShader(finalPassProgram, vertexShader);
        glAttachShader(finalPassProgram, fragmentShader);
        glLinkProgram(finalPassProgram);

        if(!checkProgram(finalPassProgram))
        {
            throw std::runtime_error("Interpolation shader linking failed");
        }

        glDetachShader(finalPassProgram, vertexShader);
        glDetachShader(finalPassProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }
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

PointCloud readPly(const std::string &filepath, float defaultPointSize)
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

        bool no_color = false;
        bool no_confidence = false;
        bool no_radius = false;

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

        position = file.request_properties_from_element("vertex", {"x", "y", "z"});
        normal = file.request_properties_from_element("vertex", {"nx", "ny", "nz"});

        try
        {
            color = file.request_properties_from_element("vertex", {"red", "green", "blue"});
        }
        catch (const std::exception &e)
        {
            no_color = true;
        }

        try
        {
            confidence = file.request_properties_from_element("vertex", {"confidence"});
        }
        catch (const std::exception &e)
        {
            no_confidence = true;
        }

        try
        {
            radius = file.request_properties_from_element("vertex", {"radius"});
        }
        catch (const std::exception &e)
        {
            no_radius = true;
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
        pcl.normal.resize(position->count);
        pcl.color.resize(position->count);
        pcl.confidence.resize(position->count);
        pcl.radius.resize(position->count);

        auto t1 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = position->buffer.size_bytes();
            if(position->t == tinyply::Type::FLOAT64){
                std::vector<double3> tmp_buffer;
                tmp_buffer.resize(position->count);
                std::memcpy(tmp_buffer.data(), position->buffer.get(), numVerticesBytes);
                std::transform(tmp_buffer.begin(), tmp_buffer.end(), pcl.position.begin(), [](double3 position){
                    return float3{(float)position.x, (float)position.y, (float)position.z};
                });
            }else{
                std::memcpy(pcl.position.data(), position->buffer.get(), numVerticesBytes);
            }
            std::transform(pcl.position.begin(), pcl.position.end(), pcl.position.begin(), [](float3 position) {
                return float3{position.x, position.y, position.z};
            });
        });

        auto t2 = std::async(std::launch::async, [&]() {
            size_t numVerticesBytes = normal->buffer.size_bytes();
            if(normal->t == tinyply::Type::FLOAT64){
                std::vector<double3> tmp_buffer;
                tmp_buffer.resize(normal->count);
                std::memcpy(tmp_buffer.data(), normal->buffer.get(), numVerticesBytes);
                std::transform(tmp_buffer.begin(), tmp_buffer.end(), pcl.normal.begin(), [](double3 normal){
                    return float3{(float)normal.x, (float)normal.y, (float)normal.z};
                });
            }else{
                std::memcpy(pcl.normal.data(), normal->buffer.get(), numVerticesBytes);
            }
            std::transform(pcl.normal.begin(), pcl.normal.end(), pcl.normal.begin(), [](float3 normal) {
                return float3{normal.x, normal.y, normal.z};
            });
        });

        auto t3 = std::async(std::launch::async, [&]() {
            if(no_color){
                pcl.color.assign(pcl.color.size(), {1, 1, 1});
            }else{
                size_t numVerticesBytes = color->buffer.size_bytes();
                std::memcpy(pcl.color.data(), color->buffer.get(), numVerticesBytes);
            }
        });

        auto t4 = std::async(std::launch::async, [&]() {
            if(no_confidence){
                pcl.confidence.assign(pcl.confidence.size(), 1.0f);
            }else{
                size_t numVerticesBytes = confidence->buffer.size_bytes();
                std::memcpy(pcl.confidence.data(), confidence->buffer.get(), numVerticesBytes);
            }
        });

        auto t5 = std::async(std::launch::async, [&]() {
            if(no_radius){
                pcl.radius.assign(pcl.radius.size(), defaultPointSize);
            }else{
                size_t numVerticesBytes = radius->buffer.size_bytes();
                std::memcpy(pcl.radius.data(), radius->buffer.get(), numVerticesBytes);
            }
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
    auto extension = path.substr(0, path.find("."));

    if(extension == "freiburg"){
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

        glm::mat4 firstTransform;
        bool first = true;
        while (ifs >> timestamp >> tx >> ty >> tz >> qx >> qy >> qz >> qw)
        {
            auto rotationQuat = glm::quat(qw, -qx, -qy, qz);
            auto rotationMat = glm::transpose(glm::toMat4(rotationQuat));
            auto translationMat = glm::translate(glm::mat4(1.0f), -glm::vec3(tx, ty, -tz));
            auto transform = rotationMat * translationMat;
            if(first){
                firstTransform = glm::inverse(transform);
                first = false;
            }
            trajectory.push_back(transform * coordinateTransform);
        }

        return trajectory;
    }else{
        std::vector<glm::mat4> trajectory;
        std::ifstream ifs;
        
        ifs.open(path);
        if (!ifs)
        {
            throw std::runtime_error("Unable to open trajectory file");
            exit(1);
        }

        while(!ifs.eof()){
            glm::mat4 m(1);

            auto coordinateTransform = glm::mat4({ 1.0f, 0.0f, 0.0f, 0.0f,
                                                   0.0f, 1.0f, 0.0f, 0.0f,
                                                   0.0f, 0.0f, 1.0f, 0.0f,
                                                   0.0f, 0.0f, 0.0f, 1.0f});
            
            ifs >> m[0][0] >> m[0][1] >> m[0][2] >> m[0][3];
            ifs >> m[1][0] >> m[1][1] >> m[1][2] >> m[1][3]; 
            ifs >> m[2][0] >> m[2][1] >> m[2][2] >> m[2][3];
            ifs >> m[3][0] >> m[3][1] >> m[3][2] >> m[3][3];


            trajectory.push_back(glm::transpose(glm::inverse(m*coordinateTransform)));
        }

        return trajectory;
    }
    
}

PYBIND11_MODULE(splat_renderer, m) {
    m.doc() = R"pbdoc(
        Splat rendering module
    )pbdoc";

    m.def("render", &render, R"pbdoc(
        Render a point cloud from a given camera trajectory and save the result in the output directory.
        Returns 0 if no errors were encountered. Throws runtime exceptions
    )pbdoc", py::arg("pointcloud"), py::arg("trajectory"), py::arg("output"), py::arg("delta") = 1, py::arg("pointSize")=1e-2, 
             py::arg("width")=640, py::arg("height")=480, py::arg("fx")=520.0, py::arg("fy")=528.0, py::arg("cx")=320.0, py::arg("cy")=240.0,
             py::arg("depthScale")=1000.0, py::arg("method")="standard", py::arg("surfaceThickness")=0.1);

    #ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
    #else
    m.attr("__version__") = "dev";
    #endif
}