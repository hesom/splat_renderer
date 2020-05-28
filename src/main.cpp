#define _USE_MATH_DEFINES

#include <iostream>
#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <stdexcept>
#include <memory>
#include <string>
#include <fstream>
#include <vector>

#include <math.h>

const int WIDTH = 720;
const int HEIGHT = 480;

GLuint vao;
GLuint vbo;
GLuint instanceVbo;
GLuint program;
int num_points;

std::vector<float> positions = {
    0.0f, 0.0f, 0.0f,
    0.0f, -0.5f, 0.0f,
    0.0f, 0.5f, 0.0f,
    -0.5f, 0.0f, 0.0f,
    0.5f, 0.0f, 0.0f,
    -0.5f, -0.5f, 0.0f,
    0.5f, 0.5f, 0.0f,
    -0.5f, 0.5f, 0.0f,
    0.5f, -0.5f, 0.0f,
};

std::string readFromFile(const std::string& path);

void initBuffers();
void initShaders();
bool checkShader(GLuint shaderId, GLuint type);
bool checkProgram(GLuint program);

std::vector<float> buildCircle(int fans, float radius);

int main(){
    GLFWwindow* window;

    if(!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwSetErrorCallback([](int error, const char* description){
        fprintf(stderr, "Error: %s\n", description);
    });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Splat Renderer", nullptr, nullptr);
    if(!window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    initBuffers();
    initShaders();

    while(!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, num_points, positions.size()/3);
        glfwSwapBuffers(window);

        glfwPollEvents();
    }

    glfwTerminate();

    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    return 0;
}

std::string readFromFile(const std::string& path){
    std::string content;
    std::ifstream fileStream(path, std::ios::in);

    if(!fileStream.is_open()){
        throw std::runtime_error("Could not load shader " + path);
    }

    std::string line = "";
    while(!fileStream.eof()){
        std::getline(fileStream, line);
        content.append(line + "\n");
    }

    fileStream.close();
    return content;
}

void initBuffers(){

    auto circle = buildCircle(100, 0.1f);

    num_points = circle.size();

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, circle.size()*sizeof(float), circle.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, positions.size()*sizeof(float), positions.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1);
    glBindVertexArray(0);
}

void initShaders(){
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    auto vertexSource = readFromFile("src/shaders/splat.vert");
    const char* vertexSource_c = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexSource_c, nullptr);
    glCompileShader(vertexShader);
    if(!checkShader(vertexShader, GL_VERTEX_SHADER))
    {
        throw std::runtime_error("Shader compilation failed");
    }
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    auto fragmentSource = readFromFile("src/shaders/splat.frag");
    const char* fragmentSource_c = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSource_c, nullptr);
    glCompileShader(fragmentShader);
    if(!checkShader(fragmentShader, GL_FRAGMENT_SHADER))
    {
        throw std::runtime_error("Shader compilation failed");
    }

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    if(!checkProgram(program))
    {
        throw std::runtime_error("Shader linking failed");
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

bool checkShader(GLuint shaderId, GLuint type){
    int success;
    char infoLog[512];
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);

    std::string typeString = type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT";

    if(!success)
    {
        glGetShaderInfoLog(shaderId, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::" << typeString <<"::COMPILATION::FAILED\n" << infoLog << std::endl;
        return false;
    }

    return true;
}

bool checkProgram(GLuint program){
    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if(!success)
    {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cout << "ERROR::PROGRAM::LINKING::FAILED\n" << infoLog << std::endl;
        return false;
    }

    return true;
}

std::vector<float> buildCircle(int fans, float radius){
    std::vector<float> vertices{ 0.0f, 0.0f, 0.0f};

    for(int i = 0; i <= fans; i++){
        float x = radius*static_cast<float>(cos((static_cast<float>(i)/static_cast<float>(fans))*2.0*M_PI));
        float y = radius*static_cast<float>(sin((static_cast<float>(i)/static_cast<float>(fans))*2.0*M_PI));

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);
    }

    return vertices;
}