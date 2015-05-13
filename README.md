San Andreas Dynamic SFX
=================================

GTA San Andreas [glitches sounds][1] when a sound file bigger than the original is imported. This is caused by the fact it uses a static sound buffer of an arbitrary size stored in [BankSlot.dat][2], so solving the issue by forking [SAAT][3] wouldn't be effective and would ultimately complicate everyone's life.

This plugin, however, makes this sound buffer dynamic with the size determined at the moment a sound or a bank are requested. There are no performance penalties due to reasons.

But... doesn't your own [Mod Loader][4] have this feature? Sure! But perhaps someone don't want to use it, perhaps a total conversion wants awesome sound effects without such a big dependency. Yes, this is a rip-off my own mod. Do **NOT** use this if you already have [Mod Loader][4] _(this detects Mod Loader's presence anyway so you should be safe)_.

### Compiling

If you are building from the source code, You'll need the following:

+ [Premake](http://industriousone.com/premake/download) 5 *(pre-built executable available in this repository root)*
+ An C++11 compiler:
    - [Visual Studio](http://www.visualstudio.com/downloads) 2013 or greater
    - [MinGW](http://mingw-w64.sourceforge.net/download.php) 4.9.0 or greater


Then, in a command-line shell go into the repository root directory and run the commands:

 + __For Visual Studio__:
 
        premake5 vs2013

    then you can compile the generated project in the build directory

 + __For MinGW__:
 
        premake5 gmake
        cd build
        mingw32-make
        cd ..

Use `premake5 --help` for more command line options.

### License

The source code is licensed under the MIT License, see LICENSE for details.




 [1]: https://www.youtube.com/watch?v=dfAKYOmcBGM&feature=youtu.be&t=1m13s
 [2]: http://www.gtamodding.com/wiki/SFX_(SA)#BankSlot.dat
 [3]: http://pdescobar.home.comcast.net/~pdescobar/gta/saat/
 [4]: http://gtaforums.com/topic/669520-sarel-mod-loader/
