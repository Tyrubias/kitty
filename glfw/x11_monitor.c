//========================================================================
// GLFW 3.4 X11 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2002-2006 Marcus Geelnard
// Copyright (c) 2006-2019 Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
// It is fine to use C99 in this file because it will not be built with VS
//========================================================================

#include "internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


// Check whether the display mode should be included in enumeration
//
static bool modeIsGood(const XRRModeInfo* mi)
{
    return (mi->modeFlags & RR_Interlace) == 0;
}

// Calculates the refresh rate, in Hz, from the specified RandR mode info
//
static int calculateRefreshRate(const XRRModeInfo* mi)
{
    if (mi->hTotal && mi->vTotal)
        return (int) round((double) mi->dotClock / ((double) mi->hTotal * (double) mi->vTotal));
    else
        return 0;
}

// Returns the mode info for a RandR mode XID
//
static const XRRModeInfo* getModeInfo(const XRRScreenResources* sr, RRMode id)
{
    for (int i = 0;  i < sr->nmode;  i++)
    {
        if (sr->modes[i].id == id)
            return sr->modes + i;
    }

    return NULL;
}

// Convert RandR mode info to GLFW video mode
//
static GLFWvidmode vidmodeFromModeInfo(const XRRModeInfo* mi,
                                       const XRRCrtcInfo* ci)
{
    GLFWvidmode mode;

    if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270)
    {
        mode.width  = mi->height;
        mode.height = mi->width;
    }
    else
    {
        mode.width  = mi->width;
        mode.height = mi->height;
    }

    mode.refreshRate = calculateRefreshRate(mi);

    _glfwSplitBPP(DefaultDepth(_glfw.x11.display, _glfw.x11.screen),
                  &mode.redBits, &mode.greenBits, &mode.blueBits);

    return mode;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Poll for changes in the set of connected monitors
//
void _glfwPollMonitorsX11(void)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        int disconnectedCount, screenCount = 0;
        _GLFWmonitor** disconnected = NULL;
        XineramaScreenInfo* screens = NULL;
        XRRScreenResources* sr = XRRGetScreenResourcesCurrent(_glfw.x11.display,
                                                              _glfw.x11.root);
        RROutput primary = XRRGetOutputPrimary(_glfw.x11.display,
                                               _glfw.x11.root);

        if (_glfw.x11.xinerama.available)
            screens = XineramaQueryScreens(_glfw.x11.display, &screenCount);

        disconnectedCount = _glfw.monitorCount;
        if (disconnectedCount)
        {
            disconnected = calloc(_glfw.monitorCount, sizeof(_GLFWmonitor*));
            memcpy(disconnected,
                   _glfw.monitors,
                   _glfw.monitorCount * sizeof(_GLFWmonitor*));
        }

        for (int i = 0;  i < sr->noutput;  i++)
        {
            int j, type, widthMM, heightMM;

            XRROutputInfo* oi = XRRGetOutputInfo(_glfw.x11.display, sr, sr->outputs[i]);
            if (oi->connection != RR_Connected || oi->crtc == None)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }

            for (j = 0;  j < disconnectedCount;  j++)
            {
                if (disconnected[j] &&
                    disconnected[j]->x11.output == sr->outputs[i])
                {
                    disconnected[j] = NULL;
                    break;
                }
            }

            if (j < disconnectedCount)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }

            XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, oi->crtc);
            if (!ci)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }
            if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270)
            {
                widthMM  = oi->mm_height;
                heightMM = oi->mm_width;
            }
            else
            {
                widthMM  = oi->mm_width;
                heightMM = oi->mm_height;
            }

            if (widthMM <= 0 || heightMM <= 0)
            {
                // HACK: If RandR does not provide a physical size, assume the
                //       X11 default 96 DPI and calculate from the CRTC viewport
                // NOTE: These members are affected by rotation, unlike the mode
                //       info and output info members
                widthMM  = (int) (ci->width * 25.4f / 96.f);
                heightMM = (int) (ci->height * 25.4f / 96.f);
            }

            _GLFWmonitor* monitor = _glfwAllocMonitor(oi->name, widthMM, heightMM);
            monitor->x11.output = sr->outputs[i];
            monitor->x11.crtc   = oi->crtc;

            for (j = 0;  j < screenCount;  j++)
            {
                if (screens[j].x_org == ci->x &&
                    screens[j].y_org == ci->y &&
                    screens[j].width == (short int)ci->width &&
                    screens[j].height == (short int)ci->height)
                {
                    monitor->x11.index = j;
                    break;
                }
            }

            if (monitor->x11.output == primary)
                type = _GLFW_INSERT_FIRST;
            else
                type = _GLFW_INSERT_LAST;

            _glfwInputMonitor(monitor, GLFW_CONNECTED, type);

            XRRFreeOutputInfo(oi);
            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);

        if (screens)
            XFree(screens);

        for (int i = 0;  i < disconnectedCount;  i++)
        {
            if (disconnected[i])
                _glfwInputMonitor(disconnected[i], GLFW_DISCONNECTED, 0);
        }

        free(disconnected);
    }
    else
    {
        const int widthMM = DisplayWidthMM(_glfw.x11.display, _glfw.x11.screen);
        const int heightMM = DisplayHeightMM(_glfw.x11.display, _glfw.x11.screen);

        _glfwInputMonitor(_glfwAllocMonitor("Display", widthMM, heightMM),
                          GLFW_CONNECTED,
                          _GLFW_INSERT_FIRST);
    }
}

// Set the current video mode for the specified monitor
//
void _glfwSetVideoModeX11(_GLFWmonitor* monitor, const GLFWvidmode* desired)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        GLFWvidmode current;
        RRMode native = None;

        const GLFWvidmode* best = _glfwChooseVideoMode(monitor, desired);
        _glfwPlatformGetVideoMode(monitor, &current);
        if (_glfwCompareVideoModes(&current, best) == 0)
            return;

        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);
        XRROutputInfo* oi = XRRGetOutputInfo(_glfw.x11.display, sr, monitor->x11.output);

        for (int i = 0;  i < oi->nmode;  i++)
        {
            const XRRModeInfo* mi = getModeInfo(sr, oi->modes[i]);
            if (!modeIsGood(mi))
                continue;

            const GLFWvidmode mode = vidmodeFromModeInfo(mi, ci);
            if (_glfwCompareVideoModes(best, &mode) == 0)
            {
                native = mi->id;
                break;
            }
        }

        if (native)
        {
            if (monitor->x11.oldMode == None)
                monitor->x11.oldMode = ci->mode;

            XRRSetCrtcConfig(_glfw.x11.display,
                             sr, monitor->x11.crtc,
                             CurrentTime,
                             ci->x, ci->y,
                             native,
                             ci->rotation,
                             ci->outputs,
                             ci->noutput);
        }

        XRRFreeOutputInfo(oi);
        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    }
}

// Restore the saved (original) video mode for the specified monitor
//
void _glfwRestoreVideoModeX11(_GLFWmonitor* monitor)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        if (monitor->x11.oldMode == None)
            return;

        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);

        XRRSetCrtcConfig(_glfw.x11.display,
                         sr, monitor->x11.crtc,
                         CurrentTime,
                         ci->x, ci->y,
                         monitor->x11.oldMode,
                         ci->rotation,
                         ci->outputs,
                         ci->noutput);

        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);

        monitor->x11.oldMode = None;
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwPlatformFreeMonitor(_GLFWmonitor* monitor UNUSED)
{
}

void _glfwPlatformGetMonitorPos(_GLFWmonitor* monitor, int* xpos, int* ypos)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);

        if (ci)
        {
            if (xpos)
                *xpos = ci->x;
            if (ypos)
                *ypos = ci->y;

            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);
    }
}

void _glfwPlatformGetMonitorContentScale(_GLFWmonitor* monitor UNUSED,
                                         float* xscale, float* yscale)
{
    if (xscale)
        *xscale = _glfw.x11.contentScaleX;
    if (yscale)
        *yscale = _glfw.x11.contentScaleY;
}

MonitorGeometry
_glfwPlatformGetMonitorGeometry(_GLFWmonitor *monitor) {
    MonitorGeometry ans = {0};
    if (!monitor) return ans;
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken) {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);

        ans.full.x = ci->x;
        ans.full.y = ci->y;

        const XRRModeInfo* mi = getModeInfo(sr, ci->mode);

        if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270) {
            ans.full.width  = mi->height;
            ans.full.height = mi->width;
        } else {
            ans.full.width  = mi->width;
            ans.full.height = mi->height;
        }

        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    } else {
        ans.full.width  = DisplayWidth(_glfw.x11.display, _glfw.x11.screen);
        ans.full.height = DisplayHeight(_glfw.x11.display, _glfw.x11.screen);
    }
    ans.workarea = *(&ans.full);

    if (_glfw.x11.NET_WORKAREA && _glfw.x11.NET_CURRENT_DESKTOP) {
        Atom* extents = NULL;
        Atom* desktop = NULL;
        const unsigned long extentCount = _glfwGetWindowPropertyX11(
            _glfw.x11.root, _glfw.x11.NET_WORKAREA, XA_CARDINAL, (unsigned char**) &extents);

        if (_glfwGetWindowPropertyX11(_glfw.x11.root, _glfw.x11.NET_CURRENT_DESKTOP, XA_CARDINAL, (unsigned char**) &desktop) > 0) {
            if (extentCount >= 4 && *desktop < extentCount / 4) {
                const int globalX = extents[*desktop * 4 + 0];
                const int globalY = extents[*desktop * 4 + 1];
                const int globalWidth  = extents[*desktop * 4 + 2];
                const int globalHeight = extents[*desktop * 4 + 3];

                if (ans.workarea.x < globalX) {
                    ans.workarea.width -= globalX - ans.workarea.x;
                    ans.workarea.x = globalX;
                }
                if (ans.workarea.y < globalY) {
                    ans.workarea.height -= globalY - ans.workarea.y;
                    ans.workarea.y = globalY;
                }
                if (ans.workarea.x + ans.workarea.width > globalX + globalWidth)
                    ans.workarea.width = globalX - ans.workarea.x + globalWidth;
                if (ans.workarea.y + ans.workarea.height > globalY + globalHeight)
                    ans.workarea.height = globalY - ans.workarea.y + globalHeight;
            }
        }
        if (extents) XFree(extents);
        if (desktop) XFree(desktop);
    }
    return ans;
}

void _glfwPlatformGetMonitorWorkarea(_GLFWmonitor* monitor, int* xpos, int* ypos, int* width, int* height) {
    MonitorGeometry ans = _glfwPlatformGetMonitorGeometry(monitor);
    if (xpos)
        *xpos = ans.workarea.x;
    if (ypos)
        *ypos = ans.workarea.y;
    if (width)
        *width = ans.workarea.width;
    if (height)
        *height = ans.workarea.height;
}

GLFWvidmode* _glfwPlatformGetVideoModes(_GLFWmonitor* monitor, int* count)
{
    GLFWvidmode* result;

    *count = 0;

    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);
        XRROutputInfo* oi = XRRGetOutputInfo(_glfw.x11.display, sr, monitor->x11.output);

        result = calloc(oi->nmode, sizeof(GLFWvidmode));

        for (int i = 0;  i < oi->nmode;  i++)
        {
            const XRRModeInfo* mi = getModeInfo(sr, oi->modes[i]);
            if (!modeIsGood(mi))
                continue;

            const GLFWvidmode mode = vidmodeFromModeInfo(mi, ci);
            int j;

            for (j = 0;  j < *count;  j++)
            {
                if (_glfwCompareVideoModes(result + j, &mode) == 0)
                    break;
            }

            // Skip duplicate modes
            if (j < *count)
                continue;

            (*count)++;
            result[*count - 1] = mode;
        }

        XRRFreeOutputInfo(oi);
        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    }
    else
    {
        *count = 1;
        result = calloc(1, sizeof(GLFWvidmode));
        _glfwPlatformGetVideoMode(monitor, result);
    }

    return result;
}

bool _glfwPlatformGetVideoMode(_GLFWmonitor* monitor, GLFWvidmode* mode) {
    bool ok = false;
    if (_glfw.x11.randr.available && !_glfw.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(_glfw.x11.display, _glfw.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(_glfw.x11.display, sr, monitor->x11.crtc);

        if (ci)
        {
            const XRRModeInfo* mi = getModeInfo(sr, ci->mode);
            if (mi) { // mi can be NULL if the monitor has been disconnected
                *mode = vidmodeFromModeInfo(mi, ci);
                ok = true;
            }

            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);
    }
    else
    {
        ok = true;
        mode->width = DisplayWidth(_glfw.x11.display, _glfw.x11.screen);
        mode->height = DisplayHeight(_glfw.x11.display, _glfw.x11.screen);
        mode->refreshRate = 0;

        _glfwSplitBPP(DefaultDepth(_glfw.x11.display, _glfw.x11.screen),
                      &mode->redBits, &mode->greenBits, &mode->blueBits);
    }
    return ok;
}

bool _glfwPlatformGetGammaRamp(_GLFWmonitor* monitor, GLFWgammaramp* ramp)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.gammaBroken)
    {
        const size_t size = XRRGetCrtcGammaSize(_glfw.x11.display,
                                                monitor->x11.crtc);
        XRRCrtcGamma* gamma = XRRGetCrtcGamma(_glfw.x11.display,
                                              monitor->x11.crtc);

        _glfwAllocGammaArrays(ramp, size);

        memcpy(ramp->red,   gamma->red,   size * sizeof(unsigned short));
        memcpy(ramp->green, gamma->green, size * sizeof(unsigned short));
        memcpy(ramp->blue,  gamma->blue,  size * sizeof(unsigned short));

        XRRFreeGamma(gamma);
        return true;
    }
    else if (_glfw.x11.vidmode.available)
    {
        int size;
        XF86VidModeGetGammaRampSize(_glfw.x11.display, _glfw.x11.screen, &size);

        _glfwAllocGammaArrays(ramp, size);

        XF86VidModeGetGammaRamp(_glfw.x11.display,
                                _glfw.x11.screen,
                                ramp->size, ramp->red, ramp->green, ramp->blue);
        return true;
    }
    else
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "X11: Gamma ramp access not supported by server");
        return false;
    }
}

void _glfwPlatformSetGammaRamp(_GLFWmonitor* monitor, const GLFWgammaramp* ramp)
{
    if (_glfw.x11.randr.available && !_glfw.x11.randr.gammaBroken)
    {
        if (XRRGetCrtcGammaSize(_glfw.x11.display, monitor->x11.crtc) != (int)ramp->size)
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "X11: Gamma ramp size must match current ramp size");
            return;
        }

        XRRCrtcGamma* gamma = XRRAllocGamma(ramp->size);

        memcpy(gamma->red,   ramp->red,   ramp->size * sizeof(unsigned short));
        memcpy(gamma->green, ramp->green, ramp->size * sizeof(unsigned short));
        memcpy(gamma->blue,  ramp->blue,  ramp->size * sizeof(unsigned short));

        XRRSetCrtcGamma(_glfw.x11.display, monitor->x11.crtc, gamma);
        XRRFreeGamma(gamma);
    }
    else if (_glfw.x11.vidmode.available)
    {
        XF86VidModeSetGammaRamp(_glfw.x11.display,
                                _glfw.x11.screen,
                                ramp->size,
                                (unsigned short*) ramp->red,
                                (unsigned short*) ramp->green,
                                (unsigned short*) ramp->blue);
    }
    else
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "X11: Gamma ramp access not supported by server");
    }
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI RRCrtc glfwGetX11Adapter(GLFWmonitor* handle)
{
    _GLFWmonitor* monitor = (_GLFWmonitor*) handle;
    _GLFW_REQUIRE_INIT_OR_RETURN(None);
    return monitor->x11.crtc;
}

GLFWAPI RROutput glfwGetX11Monitor(GLFWmonitor* handle)
{
    _GLFWmonitor* monitor = (_GLFWmonitor*) handle;
    _GLFW_REQUIRE_INIT_OR_RETURN(None);
    return monitor->x11.output;
}
