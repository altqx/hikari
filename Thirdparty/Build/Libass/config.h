#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#define CONFIG_ASM 1
// the x64 assembly is built with the library (Libass.vcxproj); without this
// ass_bitmap_engine_init never picks it and every bitmap runs the C versions
#if defined(_M_X64)
#define ARCH_X86 1
#endif
#define CONFIG_RASTERIZER 1
#define CONFIG_DIRECTWRITE 1
