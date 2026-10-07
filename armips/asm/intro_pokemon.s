.nds
.thumb

// included by armips/global.s
// Oak's intro: the cry that plays with the intro Pokemon is Turtwig (387) instead of Marill (183).
// The tutorial battle's Marill is untouched: it does not go through this call.

.open "base/arm9.bin", 0x02000000

// 0x02000C70 sits in the SDK version strings, which the game never reads.
// (user_config.s uses 0x02000C3C..0x02000C54 for the frame rate patch; this stays clear of it.)
.org 0x02000C70

intro_turtwig_cry:
    push {lr}
    mov r0, #255
    add r0, #132 // 255 + 132 = 387 = SPECIES_TURTWIG
    bl 0x02006218 // PlayCry(species, form): r1 (form) is still the caller's r5 = 0
    pop {pc}

.close

.open "base/overlay/overlay_0053.bin", 0x021E5900

.org 0x021E5900 + 0x1E98 // the "bl PlayCry" in the Oak speech after the "movs r0, #183" (Marill)
    bl intro_turtwig_cry

.close
