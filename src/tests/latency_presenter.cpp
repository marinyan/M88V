// SPDX-License-Identifier: BSD-2-Clause
// Keep the production presenter unchanged; the benchmark wraps its entry point.
#define PresentScreen RealPresentScreen
#ifndef M88_LATENCY_PRESENTER_SOURCE
#define M88_LATENCY_PRESENTER_SOURCE "../win32/screen_presenter.cpp"
#endif
#include M88_LATENCY_PRESENTER_SOURCE
