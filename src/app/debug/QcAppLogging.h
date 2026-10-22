#pragma once

#include <QLoggingCategory>

// The logging categories of the graphical client (qc_app_core and the program qtcraft), each defined
// once in QcAppLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.game": the game controller, menus, worlds and test hooks.
Q_DECLARE_LOGGING_CATEGORY(lcGame)

// "qtcraft.display": window mode, screen and frame rate limit (decision E-835).
Q_DECLARE_LOGGING_CATEGORY(lcDisplay)
// "qtcraft.gamepad": gamepads.
Q_DECLARE_LOGGING_CATEGORY(lcGamepad)

// "qtcraft.input": keyboard and mouse input.
Q_DECLARE_LOGGING_CATEGORY(lcInput)

// "qtcraft.offscreen": offscreen rendering for tests.
Q_DECLARE_LOGGING_CATEGORY(lcOffscreen)

// "qtcraft.preview": the character and item previews.
Q_DECLARE_LOGGING_CATEGORY(lcPreview)

// "qtcraft.renderer": the world renderer, pipelines and GPU resources.
Q_DECLARE_LOGGING_CATEGORY(lcRenderer)

// "qtcraft.renderer.light": the section light textures.
Q_DECLARE_LOGGING_CATEGORY(lcRendererLight)

// "qtcraft.screenshot": the screenshot hook.
Q_DECLARE_LOGGING_CATEGORY(lcScreenshot)

// "qtcraft.startup": program start: window, language, bundled mods.
Q_DECLARE_LOGGING_CATEGORY(lcStartup)

// "qtcraft.textures": texture packs.
Q_DECLARE_LOGGING_CATEGORY(lcTextures)
