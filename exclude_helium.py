"""
PlatformIO extra script — excludes ARM-only SIMD assembly files from the
LVGL build on Xtensa (ESP32-S3). Helium and NEON are ARM-only extensions;
the Xtensa assembler cannot parse the C headers those .S files include.
"""
Import("env")  # noqa: F821

_ARM_ASM_DIRS = ("helium", "neon")

def _exclude_arm_asm(node):
    """Return None to drop ARM SIMD .S files, or unchanged to keep."""
    path = str(node).lower()
    for arm_dir in _ARM_ASM_DIRS:
        if arm_dir in path:
            return None
    return node

env.AddBuildMiddleware(_exclude_arm_asm, "*.S")  # noqa: F821
