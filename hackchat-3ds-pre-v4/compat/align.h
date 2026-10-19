/*
 * La libctru actuelle ne définit plus la macro ALIGN(x) utilisée par
 * citro3d v1.6.0 (typedef struct ALIGN(8)). On la fournit ici.
 */
#ifndef HC_COMPAT_ALIGN_H
#define HC_COMPAT_ALIGN_H

#ifndef ALIGN
#define ALIGN(x) __attribute__((aligned(x)))
#endif

#endif
