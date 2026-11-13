extern "C" {
#include "ngspice/ngspice.h"
#include "ngspice/wordlist.h"
#include "ngspice/cktdefs.h"
#include "ngspice/devdefs.h"
#include "ngspice/const.h"
}

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Minimal stubs for ngspice frontend/helpers referenced by the standalone BSIM4 C code.
// They keep the reference model linkable without pulling the entire ngspice frontend.

extern "C" {

// ---- Logging helpers ----
char* tprintf(const char* fmt, ...) {
    static thread_local char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return buffer;
}

// ---- Wordlist helpers (used for warning collection) ----
void wl_append_word(wordlist** first, wordlist** last, char* word) {
    if (!first || !last) return;
    wordlist* node = static_cast<wordlist*>(std::malloc(sizeof(wordlist)));
    if (!node) return;
    node->wl_word = word ? ::strdup(word) : nullptr;
    node->wl_next = nullptr;
    node->wl_prev = *last;
    if (*last) {
        (*last)->wl_next = node;
    }
    if (*first == nullptr) {
        *first = node;
    }
    *last = node;
}

void wl_free(wordlist* wl) {
    while (wl) {
        wordlist* next = wl->wl_next;
        if (wl->wl_word) std::free(wl->wl_word);
        std::free(wl);
        wl = next;
    }
}

// ---- Device limiting helpers (no limiting for now) ----
double DEVfetlim(double vnew, double /*vold*/, double /*vt*/) { return vnew; }
double DEVlimvds(double vnew, double /*vold*/) { return vnew; }
double DEVpnjlim(double vnew, double /*vold*/, double /*vt*/, double /*vcrit*/, int* icheck) {
    if (icheck) *icheck = 1;
    return vnew;
}

// ---- Numerical integration (transient) stub ----
int NIintegrate(CKTcircuit* /*ckt*/, double* geq, double* ceq, double /*value*/, int /*cap*/) {
    if (geq) *geq = 0.0;
    if (ceq) *ceq = 0.0;
    return 0;
}

// ---- Constants used by the reference model ----
double CONSTvt0 = 0.02586419;              // kT/q at 300 K
double CONSTroot2 = 1.4142135623730951;    // sqrt(2)

// ---- Memory/front-end stubs ----
void* tmalloc(size_t num) { return std::malloc(num); }
int cp_getvar(const char* /*name*/, int /*type*/, void* /*value*/) { return 0; }

// ngspice frontend dispatch table (unused here)
IFfrontEnd* SPfrontEnd = nullptr;

} // extern "C"
