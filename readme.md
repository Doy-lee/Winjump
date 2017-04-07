# Winjump
![Winjump Screenshots](docs/winjump.png)

Winjump is a utility that allows moving between windows on the desktop quicker, for example, you're programming in Vim and want to go to some other window without moving your hand from the home keys. Or you want a text-based alternative to ALT-Tabbing which can be faster if you know exactly what application you want to switch to.

The project is currently built using the WIN32API with minimal external libraries. As a result it's a single file executable with little to no dependencies and has a light footprint.

# Usage
1. Press ALT-K (non-configurable at the moment) to activate the Window.
2. Type in desired window name to bring to front.
3. Press enter when name matches the window name.

# Build
Project is developed under Visual Studio 2015. You can build using the provided solution. There's also a build.bat file using Visual Studio build tools for command line compilation.
