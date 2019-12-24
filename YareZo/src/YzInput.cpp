#include "YzInput.h"
#include "src/Window.h"
#include "Utilities/YzLogger.h"

namespace Yarezo {

    InputHandler::InputHandler() {

		for (int i = 0; i < MAX_KEYS; i++)
		{
			m_Keys[i] = false;
		}
		for (int i = 0; i < MAX_BUTTONS; i++)
		{
			m_Buttons[i] = false;
		}
		YZ_INFO("Input Handler Created");
    }

	InputHandler::~InputHandler() {
	}

	bool InputHandler::isKeyDown(int key) {
		if (key > MAX_KEYS) {
			YZ_WARN("Requested the status of a key " + std::to_string(key) +" that is outside the range of maximum keys.");
			return false;
		}
		return m_Keys[key];
	}

	void InputHandler::setupKeyInputs(GLFWwindow* window) {
		glfwSetKeyCallback(window, InputHandler::GLFWcallback);
	}

	void InputHandler::Handle(std::shared_ptr<Camera> currentCamera) {

		auto posVec = currentCamera->getPosition();
		auto upVec = currentCamera->getUpVector();
		auto lookAtVec = currentCamera->getLookAtVector();


		if (isKeyDown(GLFW_KEY_S)) {
			posVec += (glm::normalize(lookAtVec) * 0.005f);
		}

		if (isKeyDown(GLFW_KEY_W)) {
			posVec -= (glm::normalize(lookAtVec) * 0.005f);
			YZ_INFO(glm::to_string(posVec));
		}

		if (isKeyDown(GLFW_KEY_A)) {
			posVec += (glm::normalize(glm::cross(lookAtVec, upVec)) * 0.005f);
			YZ_INFO(glm::to_string(posVec));
		}

		if (isKeyDown(GLFW_KEY_D)) {
			posVec -= (glm::normalize(glm::cross(lookAtVec, upVec)) * 0.005f);
			YZ_INFO(glm::to_string(posVec));
		}

		currentCamera->setPosition(posVec);
	}

	void InputHandler::GLFWcallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		Window* win = (Window*)glfwGetWindowUserPointer(window);
		
		auto inputHandler = win->getInputHandler();
		inputHandler->m_Keys[key] = action != GLFW_RELEASE;
		if (action == GLFW_RELEASE) {
			YZ_INFO("A key has been released");
		}
		else if (action == GLFW_REPEAT) {
			YZ_INFO("A key is being held");
		}
		else {
			YZ_INFO("A key has been pressed");
		}
	}
}