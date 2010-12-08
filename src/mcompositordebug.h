#ifndef MCOMPOSITOR_DEBUG_H
#define MCOMPOSITOR_DEBUG_H

// Enable to see when and how windows are sized.
#if 0
# define RESIZE(win, w, h)                                          \
  qDebug("%s:%u: 0x%lx -> %dx%d", __FILE__, __LINE__, (win), (w), (h))
# define MOVE_RESIZE(win, x, y, w, h)                               \
  qDebug("%s:%u: 0x%lx -> %dx%d%+d%+d", __FILE__, __LINE__,         \
         (win), (w), (h), (x), (y))
# define RECONFIG(win, x, y, w, h, mask)                            \
  qDebug("%s:%u: 0x%lx -> %dx%d%+d%+d [0x%x]", __FILE__, __LINE__,  \
         (win), (w), (h), (x), (y), (mask))
#else
# define RESIZE(...)                                /* NOP */
# define MOVE_RESIZE(...)                           /* NOP */
# define RECONFIG(...)                              /* NOP */
#endif

#endif
