# Winjump
![Winjump Screenshots](docs/winjump.png)

Winjump is a utility that allows moving between windows on the desktop quicker, for example, you're programming in Vim and want to go to some other window without moving your hand from the home keys. Or you want a text-based alternative to ALT-Tabbing which can be faster if you know exactly what application you want to switch to.

The project is currently built using the WIN32API with minimal external libraries. As a result it's a single file executable with little to no dependencies and has a light footprint.

# Usage
1. Press ALT-K (non-configurable at the moment) to activate the Window.
2. Type in desired window name to bring to front.
3. Press enter when name matches the window name.

# Build
Project is developed under Visual Studio 2015, but probably works with other version. You can either build using the IDE, but there's also provided a build.bat file using Visual Studio build tools. Only 32 bit has been configured, the 64bit components are there, just not configured into the build steps yet.

All files are included with repository, so should be able to build as is.

### Build Dependencies
* [GLFW - Multiplatform OpenGL/Window Library](http://www.glfw.org/)
* [ImGui - Immediate Mode Graphical User interface](https://github.com/ocornut/imgui)
* [GL3W - Simple OpenGL core profile loading](https://github.com/skaslev/gl3w)
