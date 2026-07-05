#ifndef __stdbool_h__
#define __stdbool_h__

#if __STDC_VERSION__ < 202311l
// For compatibility with SDK. Boo.
typedef unsigned char   bool;
#define BOOL            bool
#define true            (1)
#define false           (0)
#endif

#define TRUE            true
#define FALSE           false

#endif
