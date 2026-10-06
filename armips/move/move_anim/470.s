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

    // WHITE-OUT, long version.  Measured against Radical Red's Mega Evolution (60 frames = 1 second):
    //   Radical Red : about 30 frames to fade to white, about 65 frames fully white, about 40 frames to fade back
    //   before this : 13 frames, 36 frames (and never fully white, the glow stayed on top), 12 frames
    // The screen fade moves at a fixed speed, so the fade TO white is split in two halves with a wait between them to make it slower.
    // This worked in the recording: it took 32 frames.  (If the halves ever look wrong, use the single-fade line marked SINGLE.)
    // The fade BACK in two halves did nothing in the recording, so it is a single fade.
    //
    // 1. fade to white: frames 0 to 30.  Both Pokemon are shaded white gradually over the same 30 frames.
    callfunction 33, 5, 0, 1, 0, 8, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"      // screen: fade to white, first half (alpha 0 to 8)
    //callfunction 33, 5, 0, 1, 0, 16, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"   // SINGLE: screen: fade to white in one go (use this instead of the two halves)
    callfunction 34, 6, 2050, 0, 1, 32767, 16, 30, "NaN", "NaN", "NaN", "NaN"      // shade the attacking Pokemon white over 30 frames
    callfunction 34, 6, 2056, 0, 1, 32767, 16, 30, "NaN", "NaN", "NaN", "NaN"      // shade the opposing Pokemon white over 30 frames
    wait 12                                                                        // the first half needs about 8 frames
    callfunction 33, 5, 0, 1, 8, 16, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"     // screen: fade to white, second half (alpha 8 to 16)
    wait 18                                                                        // frame 30: the screen is fully white and both Pokemon are hidden
    // 2. fully white.  The particles are taken away only now, while nothing else is visible, so the white is clean.
    // In the last recording both Pokemon came back into view about 90 frames after the shade began, while the screen was still white,
    // so the shade is applied again here (instantly) to keep them hidden through the whole hold.
    callfunction 34, 6, 2050, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"       // attacking Pokemon: white again, instantly
    callfunction 34, 6, 2056, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"       // opposing Pokemon: white again, instantly
    unloadparticle 0                                                               // ends the pulsing ring and every other emitter of file 489
    unloadparticle 1                                                               // same for file 490
    wait 32                                                                        // fully white.  This was 65, which came out at about 150 frames in the recording.
    callfunction 34, 6, 2050, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"       // and once more right before the form changes
    callfunction 34, 6, 2056, 0, 1, 32767, 16, 0, "NaN", "NaN", "NaN", "NaN"
    // 3. the form changes while the screen is white, then the screen fades back, with the shake on the new form.
    // The fade back in two halves made no difference in the recording (it took the same 14 frames), so it is one fade again.
    transform 0
    callfunction 33, 5, 0, 1, 16, 0, 32767, "NaN", "NaN", "NaN", "NaN", "NaN"      // screen: fade back from white
    callfunction 34, 6, 2056, 0, 1, 32767, 0, 10, "NaN", "NaN", "NaN", "NaN"       // opposing Pokemon: shade back to normal
    callfunction 0x24, 5, 2, 0, 1, 4, 8 | 0x100, 0, 0, 0, 0, 0                    // the shake, now on the new form
    waitstate
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