#ifndef ICONS_H
#define ICONS_H

#include <QString>

namespace Icons {

static const QString UNDO = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3 7v6h6"></path>
  <path d="M21 17a9 9 0 0 0-9-9 9 9 0 0 0-6 2.3L3 13"></path>
</svg>
)";

static const QString ZOOM_100 = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="5" width="18" height="14" rx="2" stroke-width="2"></rect>
  <circle cx="8" cy="12" r="1.5" fill="white" stroke="none"></circle>
  <circle cx="16" cy="12" r="1.5" fill="white" stroke="none"></circle>
  <path d="M12 9v6" stroke-width="2"></path>
</svg>
)";

static const QString REDO = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 7v6h-6"></path>
  <path d="M3 17a9 9 0 0 1 9-9 9 9 0 0 1 6 2.3L21 13"></path>
</svg>
)";

static const QString ZOOM_IN = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="11" cy="11" r="8"></circle>
  <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
  <line x1="11" y1="8" x2="11" y2="14"></line>
  <line x1="8" y1="11" x2="14" y2="11"></line>
</svg>
)";

static const QString ZOOM_OUT = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="11" cy="11" r="8"></circle>
  <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
  <line x1="8" y1="11" x2="14" y2="11"></line>
</svg>
)";

static const QString FIT_SCREEN = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M8 3H5a2 2 0 0 0-2 2v3m18 0V5a2 2 0 0 0-2-2h-3m0 18h3a2 2 0 0 0 2-2v-3M3 16v3a2 2 0 0 0 2 2h3"></path>
</svg>
)";

static const QString ROTATE_LEFT = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"></path>
  <path d="M3 3v5h5"></path>
</svg>
)";

static const QString ROTATE_RIGHT = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 12a9 9 0 1 1-9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"></path>
  <path d="M21 3v5h-5"></path>
</svg>
)";

static const QString FLIP_HORIZ = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M12 3v18" stroke-dasharray="4 3 4 3 4" opacity="0.6"/>
  <polyline points="6 9 3 12 6 15" />
  <polyline points="18 9 21 12 18 15" />
  <path d="M3 12h18"/>
</svg>
)";

static const QString FLIP_VERT = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3 12h18" stroke-dasharray="4 3 4 3 4" opacity="0.6"/>
  <polyline points="9 6 12 3 15 6" />
  <polyline points="9 18 12 21 15 18" />
  <path d="M12 3v18"/>
</svg>
)";

static const QString CROP = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M6 2v14a2 2 0 0 0 2 2h14"></path>
  <path d="M18 22V8a2 2 0 0 0-2-2H2"></path>
</svg>
)";

static const QString WIN_CLOSE = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <line x1="18" y1="6" x2="6" y2="18"></line>
  <line x1="6" y1="6" x2="18" y2="18"></line>
</svg>
)";

static const QString WIN_MAXIMIZE = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
</svg>
)";

static const QString WIN_RESTORE = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M15 3h6v6"></path>
  <path d="M9 21H3v-6"></path>
  <path d="M21 3l-7 7"></path>
  <path d="M3 21l7-7"></path>
</svg>
)";

static const QString WIN_SHADE = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="6 9 12 15 18 9"></polyline>
</svg>
)";

static const QString WIN_UNSHADE = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="18 15 12 9 6 15"></polyline>
</svg>
)";


static const QString ADAPT = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="m15 15 6 6m-6-6v6m0-6h6"/>
  <path d="m9 9-6-6m6 6V3m0 6H3"/>
  <rect width="14" height="14" x="5" y="5" rx="2"/>
</svg>
)";

}

#endif // ICONS_H
