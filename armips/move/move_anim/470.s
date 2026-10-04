.nds
.thumb
.include "armips/include/animscriptcmd.s"

//mega

.create "build/move/move_anim/0_470", 0x0

MegaAnimScript:
    loadparticlefromspa 0, 489
    loadparticlefromspa 1, 490
    waitparticle

    playsepanmod 1939, -117, 0x75, 4, 2

    // The custom background was taken out for the white-out test.  To put it back, remove the // from the four lines marked BG.
    //changebg 47, 0x020001      BG
    //waitforchangebg            BG

    addparticle 0,0,3
    addparticle 0,8,3
    wait 15

    addparticle 0,1,3
    addparticle 0,2,3

    //callfunction 0x22, 6, 2, 0, 1, 0x7FFF, 10, 100, 0, 0, 0, 0
    addparticle 1, 1, 3
    addparticle 0, 3, 3
    addparticle 0, 4, 3
    addparticle 0, 5, 3
    addparticle 0, 7, 3
    wait 15
    //addparticle 1, 1, 3
    //addparticle 0, 6, 3
    //addparticle 1, 1, 3
    wait 30
    // The Solar Beam effect (particle file 107, emitters 8, 9 and 10) was removed: it showed up in the background as if Solar Beam
    // was hitting the Pokemon right before the transformation. Slot 1 keeps particle file 490 until the unloadparticle 1 at the end.
    //unloadparticle 1
    //loadparticlefromspa 1, 107
    //waitparticle
    //addparticle 1,8,3
    //addparticle 1,9,3
    //addparticle 1,10,3

    wait 15
    wait 15                                                                        // stands in for the time the shake used to take here; shorten it to start the white sooner

    // WHITE-OUT (copied from Luster Purge, armips/move/move_anim/295.s).  The screen fades to solid white and every Pokemon is shaded
    // white, then the form changes, then the shake plays on the new form while the screen fades back.
    // The last number on a shade line (callfunction 34) is how many frames the shade takes to build up to full white; 0 is instant.  It was
    // 40, so the shade was still building when the form changed, which looked like one last flash on the new form.  Now it is instant.
    callfunction 33, 5, 0, 1, 0, 16, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"     // screen: fade to white (alpha 0 to 16)
    callfunction 34, 6, 2050, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"      // shade the attacking Pokemon white, instantly
    callfunction 34, 6, 2056, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"      // shade the opposing Pokemon white, instantly
    // Both Pokemon are shaded, so both are hidden when the flash begins.  The partner slots (doubles) are left out:
    //callfunction 34, 6, 2052, 0, 1, 32767, 16, 40, "NaN", "NaN", "NaN", "NaN"   // the attacker's partner (doubles)
    //callfunction 34, 6, 2064, 0, 1, 32767, 16, 40, "NaN", "NaN", "NaN", "NaN"   // the target's partner (doubles)
    wait 30                                                                        // how long it stays white before the form changes
    //addparticle 0, 10, 3                                                         // the last two emitters were removed
    //addparticle 0, 12, 3
    unloadparticle 0                                                               // ends the pulsing ring and every other emitter of file 489 just before the form change
    unloadparticle 1                                                               // same for file 490.  (These two used to be at the end of the script, after the form change.)
    transform 0
    callfunction 33, 5, 0, 1, 16, 0, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"     // screen: fade back from white (alpha 16 to 0)
    callfunction 34, 6, 2056, 0, 1, 32767, 0, 10, "NaN", "NaN", "NaN", "NaN"      // opposing Pokemon: shade back to normal (alpha 0) over 10 frames
    callfunction 0x24, 5, 2, 0, 1, 4, 8 | 0x100, 0, 0, 0, 0, 0                    // the shake, now on the new form
    waitstate
    //wait 15                                                                      // removed so the cry starts right after the shake
    //unloadparticle 0                                                             // moved up, before the form change
    playcry 0, -117, 127
    waitcry 0
    wait 15


    //resetbg 47, 0x040001       BG

    waitstate
    //waitforchangebg            BG

    //waitparticle

    //unloadparticle 1                                                             // moved up, before the form change
    waitstate

    end

.close