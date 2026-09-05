/* Voice — web UI behaviour.
 *
 * mod-ui evaluates this file with  eval('method = ' + code)  so it must
 * hold ONE function expression and nothing else: no trailing semicolon,
 * no declaration around it.
 *
 * It does four things the markup cannot:
 *   - lights the section a switch belongs to, so ON is legible at a glance
 *   - drives the two meters from the monitored outputs
 *   - walks the program list, and pulses SAVE and TAP, which are ports
 *     that only respond to being written
 *   - lets a USER slot be given a name. The NAME lives in this browser
 *     (the plugin has no port that carries text); the SOUND lives in the
 *     plugin and travels with the pedalboard. That split is deliberate
 *     and documented, not an oversight.
 *
 * PREMIER_USER must match N_PROGRAM in programs.h and DERNIER the end of
 * the PROGRAM range in voice.ttl. check_modgui.js compares all three.
 */
function (event, funcs) {

    var PREMIER_USER = 69;
    var DERNIER = 74;
    var N_SLOT = 6;

    /* PROGRAMMES-DEBUT - written by make_ttl.py, do not edit */
    var SYMBOLES = ["low_cut", "gate", "comp", "de_ess", "body", "mid_freq", "presence", "air", "drive", "pitch", "pitch_mix", "doubler", "spread", "voices", "modulation", "mod_speed", "feedback", "delay_time", "delay_repeats", "delay_mix", "reverb", "reverb_mix", "gate_on", "comp_on", "de_ess_on", "eq_on", "drive_on", "pitch_on", "doubler_on", "mod_on", "feedback_on", "delay_on", "reverb_on"];
    var PROGRAMMES = [
        null,          /* MANUAL: a program that changes nothing */
        [120.0,-45.0,30.0,40.0,0.0,2500.0,3.0,1.0,15.0,0.0,100.0,20.0,50.0,2.0,25.0,0.6,0.0,300.0,20.0,10.0,25.0,10.0,1,1,1,1,0,1,0,0,1,0,0],  /* Speech */
        [110.0,-44.0,38.0,45.0,1.0,2400.0,2.0,2.0,12.0,0.0,100.0,15.0,50.0,3.0,20.0,0.6,0.0,250.0,15.0,6.0,20.0,6.0,1,1,1,1,0,1,0,0,1,0,0],  /* Podcast */
        [85.0,-50.0,30.0,55.0,1.0,1800.0,1.0,-1.0,10.0,0.0,100.0,15.0,25.0,2.0,18.0,0.6,25.0,220.0,12.0,8.0,18.0,7.0,1,1,1,1,0,1,0,0,0,0,0],  /* Audiobook */
        [120.0,-42.0,38.0,50.0,-2.0,3000.0,5.0,3.0,20.0,0.0,100.0,18.0,30.0,2.0,20.0,0.6,30.0,180.0,12.0,8.0,22.0,8.0,1,1,1,1,1,1,0,0,0,0,0],  /* Voice-Over */
        [150.0,-38.0,34.0,45.0,-3.0,2600.0,3.0,1.0,10.0,0.0,100.0,15.0,30.0,2.0,15.0,0.6,50.0,200.0,12.0,8.0,18.0,8.0,1,1,1,1,0,1,0,0,1,0,0],  /* Desk Mic */
        [70.0,-44.0,28.0,55.0,3.0,1600.0,2.0,2.0,14.0,-3.0,100.0,15.0,20.0,2.0,20.0,0.5,30.0,250.0,15.0,8.0,20.0,8.0,1,1,1,1,0,1,0,0,0,0,1],  /* Radio Announcer */
        [100.0,-42.0,32.0,30.0,-2.0,2200.0,2.0,2.0,20.0,0.0,100.0,25.0,50.0,2.0,30.0,0.6,0.0,350.0,25.0,12.0,35.0,10.0,1,1,1,1,0,1,0,0,1,0,1],  /* Stage Dry */
        [90.0,-48.0,30.0,30.0,2.0,2200.0,0.0,2.0,15.0,0.0,100.0,25.0,50.0,3.0,30.0,0.6,0.0,420.0,25.0,12.0,55.0,16.0,1,1,1,1,0,1,1,0,1,1,1],  /* Ballad */
        [80.0,-50.0,26.0,30.0,2.0,2000.0,1.0,3.0,15.0,0.0,100.0,28.0,55.0,3.0,28.0,0.35,25.0,500.0,30.0,13.0,70.0,18.0,1,1,1,1,0,1,1,1,0,1,1],  /* Power Ballad */
        [70.0,-50.0,28.0,30.0,2.0,900.0,-2.0,2.0,18.0,0.0,100.0,20.0,30.0,2.0,20.0,0.6,25.0,380.0,22.0,10.0,45.0,16.0,1,1,1,1,1,1,0,0,0,0,1],  /* Warm Crooner */
        [110.0,-44.0,36.0,55.0,-1.0,3200.0,4.0,5.0,15.0,0.0,100.0,26.0,30.0,2.0,20.0,0.6,25.0,300.0,20.0,10.0,35.0,14.0,1,1,1,1,0,1,1,0,0,1,1],  /* Modern Pop */
        [100.0,-44.0,32.0,55.0,1.0,2400.0,2.0,3.0,18.0,0.0,100.0,30.0,30.0,2.0,25.0,0.6,20.0,375.0,22.0,12.0,40.0,14.0,1,1,1,1,0,1,1,0,0,1,1],  /* Pop Lead */
        [130.0,-40.0,34.0,45.0,0.0,3000.0,4.0,1.0,22.0,0.0,100.0,25.0,50.0,2.0,25.0,0.6,0.0,120.0,20.0,8.0,30.0,10.0,1,1,1,1,1,1,0,0,1,1,1],  /* Rock */
        [120.0,-40.0,34.0,45.0,-1.0,3000.0,4.0,1.5,28.0,0.0,100.0,22.0,35.0,2.0,22.0,0.6,35.0,110.0,18.0,10.0,28.0,9.0,1,1,1,1,1,1,0,0,1,1,1],  /* Rock Lead */
        [150.0,-36.0,38.0,60.0,-3.0,3200.0,5.0,0.0,45.0,0.0,100.0,22.0,25.0,2.0,20.0,0.6,60.0,90.0,10.0,10.0,18.0,6.0,1,1,1,1,1,1,0,0,1,0,1],  /* Hard Rock Shout */
        [110.0,-42.0,32.0,40.0,-1.0,2800.0,3.0,3.0,20.0,0.0,100.0,20.0,40.0,2.0,20.0,0.6,25.0,130.0,6.0,20.0,30.0,10.0,1,1,1,1,1,1,0,0,1,1,1],  /* Country */
        [160.0,-38.0,40.0,50.0,-5.0,3500.0,6.0,-1.0,25.0,0.0,100.0,22.0,30.0,2.0,18.0,0.6,70.0,100.0,12.0,10.0,25.0,10.0,1,1,1,1,0,1,0,0,1,0,0],  /* Cut Through */
        [140.0,-52.0,45.0,45.0,-3.0,3200.0,2.0,5.0,15.0,0.0,100.0,30.0,40.0,2.0,25.0,0.6,0.0,320.0,20.0,10.0,45.0,20.0,1,1,1,1,0,1,1,0,1,0,1],  /* Whisper */
        [95.0,-46.0,32.0,35.0,0.0,2600.0,2.0,2.0,15.0,0.0,100.0,45.0,15.0,2.0,20.0,0.6,0.0,300.0,20.0,8.0,30.0,10.0,1,1,1,1,0,1,1,0,1,0,1],  /* Tight Double */
        [110.0,-38.0,32.0,35.0,0.0,2800.0,3.0,1.0,16.0,0.0,100.0,40.0,30.0,2.0,20.0,0.6,55.0,300.0,20.0,8.0,30.0,10.0,1,1,1,1,0,1,1,0,1,0,1],  /* Stage Double */
        [90.0,-48.0,30.0,30.0,0.0,2200.0,0.0,3.0,15.0,0.0,100.0,55.0,50.0,3.0,40.0,0.4,0.0,400.0,20.0,8.0,60.0,20.0,1,1,1,1,0,1,1,1,1,0,1],  /* Wide */
        [140.0,-40.0,34.0,45.0,-3.0,1500.0,-2.0,2.0,15.0,0.0,100.0,55.0,75.0,4.0,35.0,0.5,30.0,280.0,18.0,10.0,55.0,18.0,1,1,1,1,0,1,1,1,1,0,1],  /* Backing Vocals */
        [120.0,-44.0,30.0,45.0,-3.0,3000.0,2.0,4.0,12.0,0.0,100.0,55.0,75.0,4.0,30.0,0.45,25.0,320.0,18.0,8.0,50.0,16.0,1,1,1,1,0,1,1,1,0,0,1],  /* Stacked Backing */
        [100.0,-46.0,28.0,30.0,1.0,2200.0,0.0,2.0,15.0,0.0,100.0,60.0,50.0,4.0,45.0,0.35,0.0,350.0,20.0,8.0,70.0,18.0,1,1,1,1,0,1,1,1,1,0,1],  /* Choir */
        [100.0,-48.0,28.0,35.0,0.0,2400.0,1.0,3.0,15.0,0.0,100.0,65.0,100.0,4.0,35.0,0.3,0.0,450.0,25.0,10.0,72.0,22.0,1,1,1,1,0,1,1,1,1,0,1],  /* Wide Choir */
        [95.0,-46.0,24.0,30.0,2.0,2000.0,2.0,3.0,15.0,0.0,100.0,60.0,85.0,4.0,30.0,0.35,0.0,400.0,20.0,8.0,65.0,16.0,1,1,1,1,0,1,1,1,1,0,1],  /* Gospel Choir */
        [110.0,-44.0,24.0,30.0,1.0,2000.0,3.0,2.0,20.0,0.0,100.0,58.0,70.0,4.0,25.0,0.6,40.0,330.0,22.0,10.0,55.0,14.0,1,1,1,1,1,1,1,0,1,1,1],  /* Gospel Stack */
        [110.0,-48.0,26.0,35.0,0.0,3000.0,1.0,4.0,15.0,12.0,22.0,65.0,95.0,4.0,35.0,0.3,0.0,550.0,35.0,12.0,90.0,24.0,1,1,1,1,0,1,1,1,1,1,1],  /* Angel Choir */
        [115.0,-48.0,26.0,40.0,-1.0,3200.0,0.0,5.0,14.0,0.0,100.0,70.0,90.0,4.0,30.0,0.25,0.0,600.0,35.0,11.0,95.0,28.0,1,1,1,1,0,1,1,1,1,1,1],  /* Seraphim */
        [80.0,-46.0,30.0,25.0,3.0,1200.0,1.0,0.0,0.0,-4.0,100.0,20.0,50.0,2.0,25.0,0.6,0.0,400.0,20.0,8.0,35.0,10.0,1,1,1,1,1,1,0,0,1,0,1],  /* Baritone */
        [100.0,-46.0,30.0,35.0,0.0,2600.0,2.0,2.0,0.0,3.0,100.0,20.0,50.0,2.0,25.0,0.6,0.0,350.0,20.0,8.0,35.0,10.0,1,1,1,1,1,1,0,0,1,0,1],  /* Tenor */
        [150.0,-44.0,36.0,30.0,0.0,3000.0,3.0,0.0,0.0,9.0,100.0,20.0,50.0,2.0,30.0,0.6,0.0,250.0,20.0,8.0,20.0,8.0,1,1,1,1,1,1,0,0,1,0,1],  /* Helium */
        [90.0,-46.0,36.0,30.0,2.0,2200.0,0.0,0.0,0.0,-12.0,35.0,20.0,50.0,2.0,25.0,0.6,0.0,400.0,20.0,8.0,30.0,12.0,1,1,1,1,1,1,0,0,1,0,1],  /* Octave */
        [75.0,-46.0,32.0,25.0,4.0,1400.0,1.0,0.0,15.0,-12.0,40.0,32.0,25.0,2.0,20.0,0.6,0.0,380.0,20.0,8.0,35.0,10.0,1,1,1,1,0,1,1,0,1,0,1],  /* Octave Below */
        [70.0,-42.0,30.0,15.0,3.0,900.0,-2.0,-4.0,35.0,-7.0,65.0,40.0,70.0,3.0,25.0,0.6,0.0,400.0,25.0,9.0,50.0,14.0,1,1,1,1,1,1,1,0,1,0,1],  /* Fifth Below */
        [70.0,-40.0,28.0,20.0,2.0,900.0,-3.0,-6.0,20.0,-8.0,100.0,25.0,70.0,2.0,25.0,0.6,0.0,450.0,25.0,10.0,55.0,18.0,1,1,1,1,1,1,1,0,1,0,1],  /* Monster */
        [200.0,-38.0,75.0,20.0,-6.0,1600.0,6.0,-6.0,55.0,-5.0,60.0,40.0,10.0,3.0,55.0,6.0,0.0,90.0,30.0,10.0,20.0,8.0,1,1,1,1,1,1,1,1,1,1,1],  /* Robot */
        [180.0,-42.0,52.0,25.0,-6.0,3200.0,4.0,4.0,25.0,7.0,55.0,40.0,100.0,4.0,65.0,3.5,30.0,40.0,40.0,15.0,70.0,22.0,1,1,1,1,1,1,1,1,0,1,1],  /* Alien */
        [320.0,-42.0,45.0,20.0,-12.0,1800.0,9.0,-12.0,40.0,0.0,100.0,15.0,50.0,3.0,20.0,0.6,0.0,180.0,15.0,8.0,15.0,10.0,1,1,1,1,1,1,0,0,1,0,1],  /* Hygiaphone */
        [400.0,-40.0,58.0,25.0,-12.0,2400.0,6.0,-12.0,20.0,0.0,100.0,15.0,50.0,3.0,20.0,0.6,0.0,200.0,15.0,8.0,15.0,8.0,1,1,1,1,1,1,0,0,1,0,0],  /* Telephone */
        [380.0,-38.0,34.0,20.0,-10.0,1500.0,10.0,-10.0,40.0,0.0,100.0,15.0,50.0,3.0,25.0,0.6,0.0,120.0,20.0,10.0,25.0,12.0,1,1,1,1,1,1,0,0,1,1,1],  /* Megaphone */
        [400.0,-34.0,55.0,20.0,-12.0,2000.0,8.0,-12.0,44.0,0.0,100.0,15.0,50.0,3.0,20.0,0.6,0.0,100.0,10.0,6.0,12.0,6.0,1,1,1,1,1,1,0,0,1,0,0],  /* Walkie Talkie */
        [250.0,-42.0,40.0,40.0,-8.0,1500.0,6.0,-6.0,50.0,0.0,100.0,20.0,50.0,2.0,25.0,0.6,0.0,200.0,20.0,8.0,20.0,8.0,1,1,1,1,1,1,0,0,1,0,0],  /* Radio */
        [120.0,-42.0,32.0,35.0,0.0,2800.0,3.0,0.0,25.0,0.0,100.0,20.0,50.0,2.0,25.0,0.6,0.0,95.0,8.0,18.0,20.0,8.0,1,1,1,1,1,1,0,0,1,1,1],  /* Slapback */
        [120.0,-42.0,32.0,35.0,-1.0,2800.0,3.0,1.0,28.0,0.0,100.0,20.0,50.0,2.0,20.0,0.5,40.0,110.0,12.0,22.0,20.0,8.0,1,1,1,1,1,1,0,0,0,1,1],  /* Tape Slap */
        [110.0,-44.0,32.0,35.0,0.0,2800.0,3.0,2.0,16.0,0.0,100.0,20.0,50.0,2.0,20.0,0.6,40.0,250.0,45.0,20.0,30.0,10.0,1,1,1,1,0,1,0,0,0,1,1],  /* Eighth Notes */
        [100.0,-44.0,30.0,30.0,2.0,1800.0,0.0,1.0,16.0,0.0,100.0,20.0,50.0,3.0,30.0,0.3,0.0,480.0,75.0,14.0,60.0,13.0,1,1,1,1,1,1,0,1,1,1,1],  /* Dub */
        [110.0,-44.0,26.0,30.0,2.0,1600.0,-1.0,-2.0,26.0,0.0,100.0,20.0,50.0,2.0,30.0,0.25,35.0,520.0,78.0,20.0,60.0,14.0,1,1,1,1,1,1,0,1,0,1,1],  /* Dub Echo */
        [90.0,-50.0,24.0,30.0,0.0,2200.0,0.0,3.0,15.0,0.0,100.0,30.0,50.0,3.0,30.0,0.3,0.0,700.0,45.0,13.0,85.0,24.0,1,1,1,1,0,1,1,1,1,1,1],  /* Ambient */
        [120.0,-52.0,22.0,30.0,-2.0,3000.0,-1.0,4.0,15.0,0.0,100.0,35.0,75.0,3.0,40.0,0.2,30.0,900.0,60.0,16.0,95.0,32.0,1,1,1,1,0,1,1,1,0,1,1],  /* Ambient Wash */
        [105.0,-44.0,34.0,35.0,0.0,2800.0,3.0,2.0,18.0,0.0,100.0,30.0,60.0,3.0,25.0,0.6,0.0,500.0,30.0,12.0,75.0,20.0,1,1,1,1,0,1,1,0,1,1,1],  /* Arena */
        [110.0,-40.0,32.0,35.0,0.0,2800.0,3.0,2.0,18.0,0.0,100.0,25.0,55.0,3.0,25.0,0.6,60.0,450.0,28.0,12.0,80.0,20.0,1,1,1,1,0,1,0,0,1,1,1],  /* Stadium */
        [110.0,-46.0,26.0,35.0,0.0,2200.0,0.0,2.0,15.0,0.0,100.0,25.0,50.0,3.0,25.0,0.6,0.0,600.0,45.0,11.0,100.0,22.0,1,1,1,1,0,1,1,0,1,1,1],  /* Cathedral */
        [100.0,-50.0,26.0,30.0,1.0,2400.0,1.0,3.0,15.0,0.0,100.0,20.0,50.0,3.0,20.0,0.6,45.0,380.0,25.0,10.0,70.0,26.0,1,1,1,1,0,1,0,0,1,0,1],  /* Church */
        [120.0,-46.0,24.0,35.0,-1.0,2600.0,1.0,2.0,15.0,0.0,100.0,25.0,60.0,3.0,25.0,0.6,40.0,750.0,55.0,12.0,100.0,26.0,1,1,1,1,0,1,0,0,1,1,1],  /* Basilica */
        [130.0,-50.0,24.0,35.0,0.0,3000.0,1.0,4.0,15.0,12.0,18.0,25.0,50.0,2.0,30.0,0.2,30.0,800.0,55.0,14.0,100.0,30.0,1,1,1,1,0,1,0,1,0,1,1],  /* Shimmer */
        [95.0,-38.0,8.0,15.0,0.0,2600.0,4.0,1.0,42.0,0.0,100.0,25.0,35.0,2.0,25.0,0.6,70.0,380.0,30.0,14.0,45.0,16.0,1,1,1,1,1,1,0,0,1,1,1],  /* Guitar Solo */
        [150.0,-34.0,50.0,30.0,-5.0,2400.0,5.0,-4.0,60.0,0.0,100.0,25.0,30.0,2.0,25.0,0.5,70.0,420.0,32.0,16.0,45.0,12.0,1,1,0,1,1,1,0,0,1,1,1],  /* Lead Solo */
        [180.0,-30.0,55.0,35.0,-7.0,1600.0,7.0,-7.0,90.0,0.0,100.0,25.0,30.0,2.0,25.0,0.6,90.0,380.0,25.0,12.0,35.0,10.0,1,1,1,1,1,1,0,0,1,1,1],  /* Fuzz Lead */
        [150.0,-32.0,54.0,30.0,-5.0,2000.0,5.0,-4.0,75.0,0.0,100.0,25.0,30.0,2.0,25.0,0.6,80.0,420.0,30.0,14.0,40.0,12.0,1,1,1,1,1,1,0,0,1,1,1],  /* High Gain */
        [110.0,-36.0,18.0,10.0,-2.0,1400.0,2.0,0.0,45.0,0.0,100.0,20.0,50.0,2.0,20.0,0.6,45.0,300.0,20.0,8.0,25.0,10.0,1,1,1,1,1,1,0,0,1,0,1],  /* Guitar Crunch */
        [90.0,-44.0,25.0,10.0,0.0,1200.0,-2.0,3.0,12.0,0.0,100.0,30.0,60.0,2.0,35.0,0.5,35.0,420.0,25.0,12.0,50.0,18.0,1,1,1,1,0,1,1,1,1,1,1],  /* Guitar Clean */
        [90.0,-48.0,32.0,25.0,1.0,3200.0,-2.0,5.0,18.0,12.0,30.0,30.0,45.0,2.0,35.0,0.45,30.0,480.0,30.0,14.0,55.0,18.0,1,1,0,1,0,0,0,1,0,1,1],  /* Clean Chime */
        [100.0,-50.0,34.0,25.0,-3.0,3000.0,-2.0,4.0,12.0,0.0,100.0,25.0,50.0,2.0,25.0,0.5,60.0,400.0,20.0,10.0,45.0,16.0,1,1,0,1,0,1,0,0,1,0,1],  /* Acoustic Piezo */
        [30.0,-46.0,28.0,20.0,1.0,1000.0,1.0,-4.0,18.0,-12.0,30.0,20.0,20.0,2.0,20.0,0.5,25.0,300.0,15.0,8.0,20.0,8.0,1,1,0,1,1,0,0,0,0,0,0],  /* Bass DI */
        [160.0,-34.0,34.0,30.0,-4.0,1600.0,4.0,-5.0,50.0,0.0,100.0,20.0,30.0,2.0,25.0,0.5,75.0,110.0,18.0,15.0,30.0,10.0,1,1,0,1,1,1,0,0,1,1,1],  /* Harmonica */
        [80.0,-46.0,28.0,35.0,2.0,2600.0,-2.0,2.0,18.0,0.0,100.0,25.0,40.0,2.0,25.0,0.5,35.0,380.0,25.0,12.0,60.0,20.0,1,1,1,1,0,1,0,0,1,0,1],  /* Saxophone */
        [70.0,-50.0,28.0,20.0,0.0,1200.0,1.0,2.0,20.0,0.0,100.0,30.0,60.0,3.0,58.0,5.5,20.0,350.0,20.0,8.0,40.0,14.0,1,1,0,1,1,1,0,1,0,0,1],  /* Rotary Keys */
    ];
    /* PROGRAMMES-FIN */

    function nombre(v) {
        return (typeof v === 'number' && v === v) ? v : 0;
    }

    function borner(v, bas, haut) {
        v = nombre(v);
        return v < bas ? bas : (v > haut ? haut : v);
    }

    function etat(icon) {
        var d = icon.data('voiceState');
        if (!d) {
            d = { program: 0, ports: {} };
            icon.data('voiceState', d);
        }
        return d;
    }

    function cleNom(slot) {
        return 'voice.userName.' + slot;
    }

    function cleValeurs(slot) {
        return 'voice.userValues.' + slot;
    }

    /* The plugin keeps its own copy of every USER slot and plays it with
       no browser in sight. This second copy exists only to move the
       KNOBS: a plugin may not write its own control inputs, so the sound
       would change under a screen that still showed the sound before. */
    function lireValeurs(slot) {
        try {
            var t = window.localStorage.getItem(cleValeurs(slot));
            var v = t ? JSON.parse(t) : null;
            return (v && v.length === SYMBOLES.length) ? v : null;
        } catch (e) {
            return null;
        }
    }

    function ecrireValeurs(slot, valeurs) {
        try {
            window.localStorage.setItem(cleValeurs(slot),
                                        JSON.stringify(valeurs));
        } catch (e) { /* private mode: the plugin still has the sound */ }
    }

    function lireNom(slot) {
        try {
            return window.localStorage.getItem(cleNom(slot)) || '';
        } catch (e) {
            return '';
        }
    }

    function ecrireNom(slot, nom) {
        try {
            window.localStorage.setItem(cleNom(slot), nom);
        } catch (e) { /* private mode: the sound still saves, the name does not */ }
    }

    /* The name box only means anything on a USER slot. */
    function majProgramme(icon, valeur, slotDest) {
        var p = Math.round(borner(valeur, 0, DERNIER));
        var estUser = p >= PREMIER_USER;
        var champ = icon.find('.voice-prog-rename');
        /* The box names the slot being LOOKED at when a USER program is
           selected, and otherwise the slot SAVE would write to - which is
           the one the player is about to name. */
        var slot = estUser ? (p - PREMIER_USER + 1)
                           : Math.round(borner(slotDest || 1, 1, N_SLOT));

        champ.prop('disabled', false);
        champ.val(lireNom(slot));
        if (estUser) {
            icon.find('.voice-prog-value').text(lireNom(slot) || ('USER ' + slot));
        }
    }

    /* What the knobs say right now, in the order of SYMBOLES. */
    function lireCourant(icon) {
        var d = etat(icon), out = [], i;
        for (i = 0; i < SYMBOLES.length; i++) {
            out.push(nombre(d.ports[SYMBOLES[i]]));
        }
        return out;
    }

    /* Move the knobs to where the program says. MANUAL moves nothing -
       it means "the controls are yours" - and a USER slot moves them
       only if this browser has a copy of it. */
    function appliquer(icon, p) {
        var valeurs = null, i;
        p = Math.round(borner(p, 0, DERNIER));
        if (p >= PREMIER_USER) {
            valeurs = lireValeurs(p - PREMIER_USER + 1);
        } else if (p > 0 && p < PROGRAMMES.length) {
            valeurs = PROGRAMMES[p];
        }
        if (!valeurs) { return; }
        var d = etat(icon);
        for (i = 0; i < SYMBOLES.length; i++) {
            funcs.set_port_value(SYMBOLES[i], valeurs[i]);
            /* keep our own copy in step whether or not the host echoes
               the write back to us */
            d.ports[SYMBOLES[i]] = valeurs[i];
            majSection(icon, SYMBOLES[i], valeurs[i]);
        }
    }

    function majMetres(icon, symbol, valeur) {
        if (symbol === 'gr') {
            /* 0 dB is nothing, -24 dB is everything the meter shows */
            icon.find('.voice-gr-fill').css('width',
                (borner(-nombre(valeur) / 24, 0, 1) * 100).toFixed(1) + '%');
        } else if (symbol === 'level') {
            icon.find('.voice-level-fill').css('width',
                (borner(valeur, 0, 1) * 100).toFixed(1) + '%');
        } else if (symbol === 'fx_state') {
            var actif = nombre(valeur) > 0.5;
            icon.find('.voice-state').text(actif ? 'FX ON' : 'FX OFF')
                                     .toggleClass('actif', actif);
        } else if (symbol === 'notches') {
            var n = Math.round(nombre(valeur));
            for (var i = 0; i < 4; i++) {
                icon.find('.voice-notch-' + i).toggleClass('actif', i < n);
            }
        } else if (symbol === 'time_out') {
            icon.find('.voice-time-value').text(Math.round(nombre(valeur)) + ' ms');
        }
    }

    /* A switch lights the whole section it sits in, which is the thing the
       default interface could not do. */
    var SECTIONS = ['gate_on', 'comp_on', 'de_ess_on', 'eq_on', 'drive_on',
                    'pitch_on', 'doubler_on', 'mod_on', 'feedback_on',
                    'delay_on', 'reverb_on'];

    function majSection(icon, symbol, valeur) {
        if (SECTIONS.indexOf(symbol) < 0) { return; }
        var sw = icon.find('[mod-port-symbol="' + symbol + '"]');
        if (!sw.length) { return; }
        var on = nombre(valeur) > 0.5;
        sw.toggleClass('on', on).toggleClass('off', !on);
        if (sw.parent && sw.parent().parent) {
            sw.parent().parent().toggleClass('actif', on);
        }
    }

    function pulse(icon, symbol, classe, cible) {
        funcs.set_port_value(symbol, 1);
        cible.toggleClass('flash', true);
        window.setTimeout(function () {
            funcs.set_port_value(symbol, 0);
            cible.toggleClass('flash', false);
        }, 120);
    }

    function brancher(icon) {
        if (icon.data('voiceBound')) { return; }
        icon.data('voiceBound', true);

        icon.find('.voice-prev').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var d = etat(icon);
            d.program = borner(Math.round(d.program) - 1, 0, DERNIER);
            funcs.set_port_value('program', d.program);
            majProgramme(icon, d.program);
            appliquer(icon, d.program);
        });

        icon.find('.voice-next').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var d = etat(icon);
            d.program = borner(Math.round(d.program) + 1, 0, DERNIER);
            funcs.set_port_value('program', d.program);
            majProgramme(icon, d.program);
            appliquer(icon, d.program);
        });

        icon.find('.voice-save').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            /* SAVE always has a destination: USER SLOT says which. */
            var d = etat(icon);
            var slot = Math.round(borner(d.ports['user_slot'] || 1, 1, N_SLOT));
            pulse(icon, 'save', 'flash', icon.find('.voice-save'));
            /* The plugin has just stored the sound. Keep the same values
               here for the knobs, then GO to the slot: a save you cannot
               see is a save you do not believe in. */
            ecrireValeurs(slot, lireCourant(icon));
            var p = PREMIER_USER + slot - 1;
            d.program = p;
            funcs.set_port_value('program', p);
            majProgramme(icon, p);
        });

        icon.find('.voice-tap').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            pulse(icon, 'tap', 'flash', icon.find('.voice-tap'));
        });

        /* Typing in the pedal must not drag it around the board. */
        icon.find('.voice-prog-rename').on('mousedown', function (e) {
            if (e && e.stopPropagation) { e.stopPropagation(); }
        });
        icon.find('.voice-prog-rename').on('change', function (e) {
            var d = etat(icon);
            var p = Math.round(d.program);
            var nom = (e && e.target && typeof e.target.value === 'string')
                    ? e.target.value : '';
            var slot = (p >= PREMIER_USER) ? (p - PREMIER_USER + 1)
                     : Math.round(borner(d.ports['user_slot'] || 1, 1, N_SLOT));
            ecrireNom(slot, nom);
            if (p >= PREMIER_USER) {
                icon.find('.voice-prog-value').text(nom || ('USER ' + slot));
            }
        });
    }

    function changement(icon, symbol, valeur) {
        var d = etat(icon);
        d.ports[symbol] = valeur;
        if (symbol === 'program') {
            var avant = d.program;
            d.program = nombre(valeur);
            majProgramme(icon, valeur, d.ports['user_slot']);
            /* Only on a real change, and never while the pedalboard is
               still loading: applying at start would overwrite the sound
               the board was saved with. */
            if (d.demarre && Math.round(avant) !== Math.round(d.program)) {
                appliquer(icon, d.program);
            }
        } else if (symbol === 'user_slot') {
            majProgramme(icon, d.program, valeur);
        }
        majMetres(icon, symbol, valeur);
        majSection(icon, symbol, valeur);
    }

    if (event.type === 'start') {
        brancher(event.icon);
        var ports = event.ports || [];
        for (var i = 0; i < ports.length; i++) {
            changement(event.icon, ports[i].symbol, ports[i].value);
        }
        majProgramme(event.icon, etat(event.icon).program,
                     etat(event.icon).ports['user_slot']);
        etat(event.icon).demarre = true;
    } else if (event.type === 'change') {
        changement(event.icon, event.symbol, event.value);
    }
}
