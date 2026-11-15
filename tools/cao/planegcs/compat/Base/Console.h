// Base/Console.h : ce que PlaneGCS (FreeCAD, src/Mod/Sketcher/App/planegcs) prend de la base de FreeCAD, réduit
// à son usage : le journal du solveur. Les messages « log » ne sortent que si la variable d'environnement
// PLANEGCS_LOG existe ; les avertissements vont toujours sur stderr.
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace Base
{
class ConsoleSingleton
{
public:
    template<typename... A>
    void log(const char* fmt, A... args)
    {
        if (std::getenv("PLANEGCS_LOG")) {
            std::fprintf(stderr, fmt, args...);
        }
    }
    void log(const char* s)
    {
        if (std::getenv("PLANEGCS_LOG")) {
            std::fputs(s, stderr);
        }
    }
    template<typename... A>
    void warning(const char* fmt, A... args)
    {
        std::fprintf(stderr, fmt, args...);
    }
    void warning(const char* s)
    {
        std::fputs(s, stderr);
    }
};

inline ConsoleSingleton& Console()
{
    static ConsoleSingleton c;
    return c;
}

// (seulement avec PROFILE_DIAGNOSE)
class TimeElapsed
{
public:
    TimeElapsed() : t(std::chrono::steady_clock::now()) {}
    static double diffTimeF(const TimeElapsed& a, const TimeElapsed& b)
    {
        return std::chrono::duration<double>(b.t - a.t).count();
    }

private:
    std::chrono::steady_clock::time_point t;
};
}  // namespace Base
