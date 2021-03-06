// This example is heavily based on the tutorial at https://open.gl

// OpenGL Helpers to reduce the clutter
#include "Helpers.h"

// GLFW is necessary to handle the OpenGL context
#include <GLFW/glfw3.h>

// Linear Algebra Library
#include <Eigen/Core>

// Timer
#include <chrono>

#include <iostream>
#include <vector>
#include <deque>
#include <Eigen/Geometry>
#include <Eigen/LU>

#define GRAVITATIONALACCEL 9.80665
#define METERSPERWORLDUNITS 0.0518226   //calculated on the assumption that the diameter of a block is 1.5 inches

// VertexBufferObject wrapper
Program program;
GLFWwindow* window;

std::vector<MeshObject*> meshObjects;

float* view_A_pointer = new float[16];

std::chrono::time_point<std::chrono::high_resolution_clock> t_last;
std::deque<double> cursorXVelocities;
std::deque<double> cursorXSamples;
double currAcceleration;

double frictionCoeff;

int cheatMode;


class ViewTransformations
{
public:
    ViewTransformations(double x, double y, double z) : view_A(4,4), cam_A(4,4), window_A(4,4), camPos(x, y, z){
        view_A.setIdentity(4,4);
        cam_A.setIdentity(4,4);
        window_A.setIdentity(4,4);
        //initial view
        windowyShift = -0.3;
        cam_A *= 3.0/4.0;
    }
    void updateView(int code = -1) {
        //zoom out
        if(code == 0){
            cam_A *= 0.8;
        }
        
        //zoom in
        else if(code == 1){
            cam_A *= 1.2;
        }
        
        //update scale to size of window (assuming the correct ratio is 600x600)
        window_A.setIdentity(4,4);
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        double xScale = 1.0/((double)width/600);
        double yScale = 1.0/((double)height/600);
        window_A.col(0) << xScale * window_A.col(0);
        window_A.col(1) << yScale * window_A.col(1);
        
        setView();
    }
    
    void transformCamPos(Eigen::Vector3f translation){
        camPos = camPos+translation;
    }
    Eigen::MatrixXf getMCam(){
        //compute w
        w = camPos.normalized();
        
        //compute u
        Eigen::Vector3f positiveY(0,1,0);
        u = w.cross(positiveY).normalized()*(-1.0);
        
        //compute v
        v = w.cross(u);
        
        Eigen::MatrixXf MCam(4,4);
        MCam << u, v, w, camPos,
        0, 0, 0, 1;
        MCam = MCam.inverse();
        return MCam;
    }
    
    //set visible space
    void setVisibleWorldlbn(double x, double y, double z){lbn << x,y,z;}
    void setVisibleWorldrtf(double x, double y, double z){rtf << x,y,z;}
    
    Eigen::MatrixXf getMOrth(){
        double l = lbn.x();
        double b = lbn.y();
        double n = lbn.z();
        double r = rtf.x();
        double t = rtf.y();
        double f = rtf.z();
        
        Eigen::MatrixXf MOrth(4,4);
        MOrth << 2.0/(r-l), 0, 0, (-1.0)*(r+l)/(r-l),
        0, 2.0/(t-b), 0, (-1.0)*(t+b)/(t-b),
        0, 0, -2.0/(n-f), (-1.0)*(n+f)/(n-f),
        0, 0, 0, 1;
        
        return MOrth;
    }
    
    Eigen::MatrixXf getM(){
        return (getMOrth()*getMCam());
    }
    
    double windowyShift;
    Eigen::Vector3f camPos;
    Eigen::MatrixXf cam_A;
    float* M_pointer = new float[16];
    Eigen::MatrixXf view_A;
    
private:
    void setView() {
        view_A = window_A * cam_A;
        update_pointer(view_A_pointer, view_A);
        glUniformMatrix4fv(program.uniform("view"), 1, true, view_A_pointer);
    }
    
    Eigen::MatrixXf window_A;
    
    Eigen::Vector3f w;
    Eigen::Vector3f u;
    Eigen::Vector3f v;
    Eigen::Vector3f lbn;
    Eigen::Vector3f rtf;
};

ViewTransformations* viewTrans;

Eigen::Vector2f getCursorPosInWorld() {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    // Get the size of the window
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen position to world coordinates
    double xworld = ((xpos/double(width))*2)-1;
    double yworld = (((height-1-ypos)/double(height))*2)-1; // NOTE: y axis is flipped in glfw
    
    Eigen::MatrixXf pointTransform = viewTrans->view_A * viewTrans->getM();
    pointTransform = pointTransform.inverse();
    Eigen::Vector4f cursorPos4f(xworld, yworld + viewTrans->windowyShift*(-1.0), 0, 1);
    cursorPos4f = pointTransform * cursorPos4f;
    
    //shift to account for camera and zoom
    Eigen::Vector3f viewShift = viewTrans->camPos * (-1.0);
    viewShift(2) = 0;
    viewShift *= 1.0/viewTrans->cam_A(0,0);
    Eigen::Vector3f cursorPos(cursorPos4f.x(), cursorPos4f.y(), cursorPos4f.z());
    cursorPos = cursorPos + viewShift;
    
    xworld = cursorPos.x();
    yworld = cursorPos.y();
    
    return *(new Eigen::Vector2f(xworld, yworld));
}

void updateHammerPos() {
    Eigen::Vector2f cursorPos = getCursorPosInWorld();
    Eigen::Vector3f cursor3f(cursorPos.x(), cursorPos.y(), 0.0);
    meshObjects[6]->translate(meshObjects[6]->center, cursor3f);
}

void drawMeshObjects() {
    for(MeshObject* object : meshObjects){
        // The vertex shader wants the position of the vertices as an input.
        // The following line connects the VBO we defined above with the position "slot"
        // in the vertex shader
        program.bindVertexAttribArray("position",*(object->VBO));
        program.bindVertexAttribArray("texcoord",*(object->TCBO));
        program.bindVertexAttribArray("normal",*(object->NBO));
        glUniform1i(program.uniform("textured"),object->textured);
        if(object->textured)
            glUniform1i(program.uniform("tex"), object->texIndex);
        else
            glUniform3f(program.uniform("triangleColor"), object->solidColor.x(), object->solidColor.y(), object->solidColor.z());
        //check_gl_error();
        glUniformMatrix4fv(program.uniform("Transformation"), 1, true, object->T_pointer);
        glDrawArrays(GL_TRIANGLES, 0, object->VFull.cols());
    }
}

void sampleCursorVel() {
    // Set the uniform value depending on the time difference
    auto t_now = std::chrono::high_resolution_clock::now();
    float interval = std::chrono::duration_cast<std::chrono::duration<float>>(t_now - t_last).count();
    t_last = t_now;
    Eigen::Vector2f cursorPos = getCursorPosInWorld();
    double lastMeasuredVelocity;
    
    if(cursorXSamples.size() == 0){
        cursorXSamples.push_back(cursorPos.x());
    }
    else {
        if(cursorXVelocities.size() > 2){
            cursorXVelocities.pop_front();
            cursorXSamples.pop_front();
        }
        if(cursorXVelocities.size() > 1)
            lastMeasuredVelocity = cursorXVelocities.back();
        cursorXVelocities.push_back((cursorPos.x() - cursorXSamples.back())/interval);
        cursorXSamples.push_back(cursorPos.x());
        
        if(cursorXVelocities.size() > 1){
            currAcceleration = ((cursorXVelocities.back() - lastMeasuredVelocity)/interval)*METERSPERWORLDUNITS;
        }
            
    }
}

void checkForHit() {
    //check if hammer faces are inside bounds of a block
    Block* currBlock;
    Hammer* hammer = (Hammer*)(meshObjects[6]);
    Eigen::Vector3f leftFace = hammer->getTransformed(hammer->leftFace);
    Eigen::Vector3f rightFace = hammer->getTransformed(hammer->rightFace);
    double xMinBound;
    double xMaxBound;
    for(size_t i = 0; i < 6; i++) {
        currBlock = (Block*)(meshObjects[i]);
        xMinBound = currBlock->getTransformed(*(new Eigen::Vector3f(currBlock->xMinBound,0,0))).x();
        xMaxBound = currBlock->getTransformed(*(new Eigen::Vector3f(currBlock->xMaxBound,0,0))).x();
        if(leftFace.y() < currBlock->yMaxBound && leftFace.y() > currBlock->yMinBound && currBlock->state == "base"){
            if(leftFace.x() <= xMaxBound && leftFace.x() >= xMinBound){
                currBlock->hit(cursorXVelocities, cursorXSamples.back()+5, leftFace, currAcceleration, cheatMode);
                //std::cout << "hit block " << i << " \n";
            }
            else if(rightFace.x() >= xMinBound && rightFace.x() <= xMaxBound) {
                currBlock->hit(cursorXVelocities, cursorXSamples.back()-5, rightFace, currAcceleration, cheatMode);
            }
        }
    }
}

void resetGame(){
    for(int i = 0; i < 6; i++) {
        ((Block*)(meshObjects[i]))->reset();
    }
    ((Block*)(meshObjects[0]))->state = "base";
    
    meshObjects[5]->texIndex = 0;
}

int shift_on = 0;
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    switch (key)
    {
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:
            if(action == GLFW_PRESS)
                shift_on = 1;
            else if(action == GLFW_RELEASE)
                shift_on = 0;
            break;
        case GLFW_KEY_EQUAL:
            if(shift_on && action == GLFW_PRESS)
                viewTrans->updateView(1);
            break;
        case GLFW_KEY_MINUS:
            if(action == GLFW_PRESS)
                viewTrans->updateView(0);
            break;
        case GLFW_KEY_SPACE:
            resetGame();
            break;
        case GLFW_KEY_C:
            if(action == GLFW_PRESS){
                if(!cheatMode)
                    cheatMode = 1;
                else
                    cheatMode = 0;
            }
            break;
        default:
            break;
    }
}

void initPhysicalLaws(double staticFriction = 0.4, double leniency = 1, double mass = 0.035) {
    frictionCoeff = staticFriction;
    
    double fg = mass * GRAVITATIONALACCEL;
    double maxFrictionalForce = fg * frictionCoeff;
    double targetAccel = maxFrictionalForce / mass;
    std::cout << "target acceleration: " << targetAccel << "m/s^2 \n";
    
    double wuPerMeters = 1.5 / (0.7352 * 39.3701);
    std::cout << "wuPerMeters: " << wuPerMeters << "\n";
    
    for(int i = 0; i < 6; i++) {
        ((Block*)(meshObjects[i]))->minTargetAccel = targetAccel - leniency;
        ((Block*)(meshObjects[i]))->maxTargetAccel = targetAccel + leniency;
    }
    
    ((Block*)(meshObjects[0]))->state = "base";
}

//returns 1 if the player won, -1 if the player lost, and 0 if the game is not over yet
int gameState() {
    if(((Block*)(meshObjects[5]))->state == "base")
        return 1;
    for(int i = 0; i < 6; i++) {
        if(((Block*)(meshObjects[i]))->state == "boo")
            return -1;
    }
    return 0;
}

int main(void)
{
    // Initialize the library
    if (!glfwInit())
        return -1;

    // Activate supersampling
    glfwWindowHint(GLFW_SAMPLES, 8);

    // Ensure that we get at least a 3.2 context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

    // On apple we have to load a core profile with forward compatibility
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create a windowed mode window and its OpenGL context
    window = glfwCreateWindow(800, 600, "Daruma Otoshi Game", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);

    // Initialize the VAO
    // A Vertex Array Object (or VAO) is an object that describes how the vertex
    // attributes are stored in a Vertex Buffer Object (or VBO). This means that
    // the VAO is not the actual object storing the vertex data,
    // but the descriptor of the vertex data.
    VertexArrayObject VAO;
    VAO.init();
    VAO.bind();

    // Initialize the OpenGL Program
    // A program controls the OpenGL pipeline and it must contains
    // at least a vertex shader and a fragment shader to be valid
    const GLchar* vertex_shader =
            "#version 150 core\n"
                    "in vec3 position;"
                    "in vec2 texcoord;"
                    "in vec3 normal;"
                    "uniform mat4 view;"
                    "uniform mat4 M;"
                    "uniform mat4 Transformation;"
                    "uniform float windowShift;"
                    "out vec3 Position;"
                    "out vec2 Texcoord;"
                    "out vec3 Normal;"
                    "void main()"
                    "{"
                    "    vec4 vec4pos = vec4(position[0],position[1],position[2],1.0);"
                    "    mat4 newM = view * (M * Transformation);"
                    "    vec4 newPos = newM * vec4pos;"
                    "    gl_Position = vec4(newPos.x, newPos.y + windowShift, newPos.z, 1.0);"
    
                    "    Position = position;"
                    "    Texcoord = texcoord;"
                    "    Normal = normal;"
                    "}";
    const GLchar* fragment_shader =
            "#version 150 core\n"
                    "in vec3 Position;"
                    "in vec2 Texcoord;"
                    "in vec3 Normal;"
                    "out vec4 outColor;"
                    "uniform bool textured;"
                    "uniform vec3 triangleColor;"
                    "uniform sampler2D tex;"
                    "uniform vec3 lightPos;"
                    "uniform float ambient;"
                    "void main()"
                    "{"
                    "    if(textured){"
                    "        outColor = texture(tex, Texcoord);"
                    "    }"
                    "    else{"
                    "        outColor = vec4(triangleColor, 1.0);"
                    "    }"
    
                    "    vec3 lightRay = normalize(lightPos - Position);"
                    "    float diffuse = max(dot(Normal,lightRay), 0.0);"
                    "    outColor = outColor * min(diffuse + ambient, 1.0);"
                    "}";

    // Compile the two shaders and upload the binary to the GPU
    // Note that we have to explicitly specify that the output "slot" called outColor
    // is the one that we want in the fragment buffer (and thus on screen)
    program.init(vertex_shader,fragment_shader,"outColor");
    program.bind();

    // Save the current time --- it will be used to dynamically change the triangle color
    t_last = std::chrono::high_resolution_clock::now();

    // Register the keyboard callback
    glfwSetKeyCallback(window, key_callback);
    
    //--------------------------------------------------------------------------------------------
    
    glUniform1i(program.uniform("textured"),0);
    
    cheatMode = 0;
    
    viewTrans = new ViewTransformations(0,0.5,4);
    viewTrans->setVisibleWorldlbn(-1.5,-1.5,1.5);
    viewTrans->setVisibleWorldrtf(1.5,1.5,-1.5);
    viewTrans->updateView();
    update_pointer(viewTrans->M_pointer, viewTrans->getM());
    glUniformMatrix4fv(program.uniform("M"), 1, false, viewTrans->M_pointer);
    glUniform1f(program.uniform("windowShift"), viewTrans->windowyShift);
    
    
    glUniform3f(program.uniform("lightPos"), 1.0f, 4.0f, 2.0f);
    glUniform1f(program.uniform("ambient"), 0.5f);
    
    //read in object data
    Eigen::Matrix<double, -1, -1, 0, -1, -1> VM;
    Eigen::Matrix<double, -1, -1, 0, -1, -1> TCM;
    Eigen::Matrix<double, -1, -1, 0, -1, -1> NM;
    Eigen::Matrix<int, -1, -1, 0, -1, -1> FM;
    Eigen::Matrix<int, -1, -1, 0, -1, -1> FTCM;
    Eigen::Matrix<int, -1, -1, 0, -1, -1> FNM;
    for(int i = 0; i < 6; i++){
        igl::readOBJ("../data/darumaotoshi_obj/darumaotoshi_obj.obj", i, VM, TCM, NM, FM, FTCM, FNM);
        meshObjects.push_back(new Block(
                                             VM.transpose().cast<float>(),
                                             TCM.transpose().cast<float>(),
                                             NM.transpose().cast<float>(),
                                             FM.transpose().cast<float>(),
                                             FTCM.transpose().cast<float>(),
                                             FNM.transpose().cast<float>()));
    }
    igl::readOBJ("../data/darumaotoshi_obj/darumaotoshi_obj.obj", 6, VM, TCM, NM, FM, FTCM, FNM);
    meshObjects.push_back(new Hammer(
                                    VM.transpose().cast<float>(),
                                    TCM.transpose().cast<float>(),
                                    NM.transpose().cast<float>(),
                                    FM.transpose().cast<float>(),
                                    FTCM.transpose().cast<float>(),
                                    FNM.transpose().cast<float>()));
    
    //switch the objects at index 1 and 5, since the top block needs a texture added
    MeshObject* temp = meshObjects[5];
    meshObjects[5] = meshObjects[1];
    meshObjects[1] = temp;
    
    //reorder blocks so that they are from bottom to top in array
    for(size_t i = 1; i < 4; i++) {
        temp = meshObjects[i];
        meshObjects[i] = meshObjects[i+1];
        meshObjects[i+1] = temp;
    }
    
    //init each block's below and above pointers
    ((Block*)(meshObjects[0]))->below = (Block*)nullptr;
    for(int i = 0; i < 5; i++) {
        ((Block*)(meshObjects[i]))->above = ((Block*)(meshObjects[i+1]));
        ((Block*)(meshObjects[i+1]))->below = ((Block*)(meshObjects[i]));
    }
    ((Block*)(meshObjects[5]))->above = (Block*)nullptr;
    
    //set colors of bottom 5 blocks blocks
    meshObjects[0]->solidColor << 0.0, 0.5, 0.0;
    meshObjects[1]->solidColor << 1.0, 0.0, 1.0;
    meshObjects[2]->solidColor << 1.0, 1.0, 0.0;
    meshObjects[3]->solidColor << 1.0, 0.0, 0.0;
    meshObjects[4]->solidColor << 0.0, 1.0, 1.0;
    
    //texture setup
    std::vector<std::string> textureFiles;
    textureFiles.push_back("../data/darumaotoshi_obj/atama.png");
    textureFiles.push_back("../data/darumaotoshi_obj/hammer_c.JPG");
    textureFiles.push_back("../data/darumaotoshi_obj/white_daruma.jpg");
    textureFiles.push_back("../data/darumaotoshi_obj/gudetama.jpg");
    std::vector<int> glTextures;
    glTextures.push_back(GL_TEXTURE0);
    glTextures.push_back(GL_TEXTURE1);
    glTextures.push_back(GL_TEXTURE2);
    glTextures.push_back(GL_TEXTURE3);
    
    //set textures of top block and hammer
    GLuint textures[textureFiles.size()];
    glGenTextures(textureFiles.size(), textures);
    int width, height; unsigned char* image;
    for(size_t i = 5; i < meshObjects.size()+2; i++){
        glActiveTexture(glTextures[i-5]);
        glBindTexture(GL_TEXTURE_2D, textures[i-5]);
        image = SOIL_load_image(textureFiles[i-5].c_str(), &width, &height, 0, SOIL_LOAD_RGB);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
        SOIL_free_image_data(image);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        if(i <= meshObjects.size()-1){
            meshObjects[i]->textured = 1;
            meshObjects[i]->texIndex = i-5;
        }
    }
    
    ((Hammer*)meshObjects[6])->initialState(90);
    
    initPhysicalLaws();
    
    int immediateGameState = 0;
    
    // Loop until the user closes the window
    while (!glfwWindowShouldClose(window))
    {
        viewTrans->updateView();
        
        updateHammerPos();
        
        sampleCursorVel();
        checkForHit();
        for(int i = 0; i < 6; i++) {
            ((Block*)(meshObjects[i]))->updatePos();
        }
        
        // Bind your VAO (not necessary if you have only one)
        VAO.bind();

        // Bind your program
        program.bind();

        // Clear the framebuffer
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Enable depth test
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        immediateGameState = gameState();
        if(immediateGameState == 1){
            meshObjects[5]->texIndex = 2;
        }
        else if(immediateGameState == -1) {
            meshObjects[5]->texIndex = 3;
        }
        
        //draw objects in scene
        drawMeshObjects();

        // Swap front and back buffers
        glfwSwapBuffers(window);

        // Poll for and process events
        glfwPollEvents();
    }

    // Deallocate opengl memory
    program.free();
    VAO.free();
    //VBO.free();
    
    for(MeshObject* mo : meshObjects){
        mo->VBO->free();
        mo->TCBO->free();
        delete mo;
    }
    meshObjects.clear();

    // Deallocate glfw internals
    glfwTerminate();
    return 0;
}
