## Current issues in Zen:

> ClassiCube closes upon clicking Singleplayer button in menu. Most probably the missing functionality thats needed to open another game window.

> Providing an argument to ClassiCube skips menu and starts a world with that name. But mostly page faults, and rarely does work but the entire rendering engine looks messed up. Read following issues to get an idea why its messed up.

> ClassiCube doesn't even launch from other real shells like ash and sh (BusyBox), other than ZenOS's shell, which works because it uses a custom spawn() instead of fork + exec. Infact launching classicube from one of these shells freezes the system.

> All of this points to core memory issues. Other big clue of this is that launching nk_widgets, or classicube, or imgview, or other big apps (i.e big binary, or an app that loads a lot of memory at runtime), actually can mess up their own window buffer (this does not sound serious as obviously apps can access window buffer but its dangerous) but they can ALSO CORRUPT the window titlebars background and colors, and harp's bottom dock islands' blur and background with corrupt ahh pixel garbage.

> imgview app almost never works correctly and always has some garbage pixel lines and artifacts in it, but imgview app itself is damn simple... its memory corruption too.

---

### What to do:

> This is definitely a kernel-side or core userspace-side error.

> Completely debug out, rewrite, and fix fork, and also check spawn() in the meanwhile.

> Check and debug the core memory manager, PMM and VMM

> Also need to consider edge cases of memory running out, heap, etc.

> Shift the custom shell to use fork() again, and test ALL apps.

> Make the fork, spawn, exec stuff better, idk atp