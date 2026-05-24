APP = classicube

SRCS = \
	src/Animations.c \
	src/Audio.c \
	src/Audio_Null.c \
	src/AxisLinesRenderer.c \
	src/Bitmap.c \
	src/Block.c \
	src/BlockPhysics.c \
	src/Builder.c \
	src/Camera.c \
	src/Chat.c \
	src/Commands.c \
	src/Deflate.c \
	src/Drawer.c \
	src/Drawer2D.c \
	src/Entity.c \
	src/EntityComponents.c \
	src/EntityRenderers.c \
	src/EnvRenderer.c \
	src/Event.c \
	src/ExtMath.c \
	src/FancyLighting.c \
	src/Formats.c \
	src/Game.c \
	src/GameVersion.c \
	src/Generator.c \
	src/Graphics_SoftGPU.c \
	src/Gui.c \
	src/HeldBlockRenderer.c \
	src/Http_Worker.c \
	src/Input.c \
	src/InputHandler.c \
	src/Inventory.c \
	src/IsometricDrawer.c \
	src/LBackend.c \
	src/LScreens.c \
	src/LWeb.c \
	src/LWidgets.c \
	src/Launcher.c \
	src/Lighting.c \
	src/Logger.c \
	src/MapRenderer.c \
	src/MenuOptions.c \
	src/Menus.c \
	src/Model.c \
	src/Options.c \
	src/PackedCol.c \
	src/Particle.c \
	src/Physics.c \
	src/Picking.c \
	src/PluginAPI.c \
	src/Protocol.c \
	src/Queue.c \
	src/Resources.c \
	src/Screens.c \
	src/SelOutlineRenderer.c \
	src/SelectionBox.c \
	src/Server.c \
	src/Stream.c \
	src/String.c \
	src/SystemFonts.c \
	src/TexturePack.c \
	src/TouchUI.c \
	src/Utils.c \
	src/Vectors.c \
	src/Vorbis.c \
	src/Widgets.c \
	src/Window_ZenOS.c \
	src/World.c \
	src/Platform_ZenOS.c

CPPFLAGS += -I src -DCC_BUILD_MANUAL -DCC_BUILD_ZENOS
CFLAGS += -std=gnu99 -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-missing-braces -Wno-pointer-sign -Wno-int-conversion -Wno-deprecated-non-prototype
USER_OPT ?= -O2

include ../../mlibc_app.mk
