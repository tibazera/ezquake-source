package org.ezquake.android;

import java.util.ArrayList;
import java.util.List;

// Data-only list of files needed to run ezQuake, sourced from the nQuake
// installer's data repository (https://github.com/nQuake/distfiles), used by
// GameDataInstaller to download them into BASEDIR on first launch instead of
// requiring the user to copy files manually.
//
// Generated from a real listing of the nQuake/distfiles repository tree
// (GitHub API, master branch) and cross-checked against a real nQuake
// installation -- every remote path below is confirmed to exist, every size
// is the real file size in bytes, not an estimate.
//
// Deliberately excluded from both manifests: addon-fortress/** (Team
// Fortress) and addon-clanarena/** (Clan Arena) are never referenced here.
// non-gpl/qw/sound/ only contains non-gpl/qw/sound/ca/*.wav (Clan Arena
// sounds; the game's own sounds already ship inside the .pk3 files below),
// so it is skipped entirely rather than filtered file-by-file.
// non-gpl/qw/matchinfo/** (sample demos) is not essential to run the game
// and is skipped to keep the automatic download smaller.
public final class GameDataManifest {

    public static final class Entry {
        public final String remotePath; // relative to https://raw.githubusercontent.com/nQuake/distfiles/master/
        public final String localPath;  // relative to BASEDIR
        public final long approxSize;   // bytes, used only for the progress bar total

        Entry(String remotePath, String localPath, long approxSize) {
            this.remotePath = remotePath;
            this.localPath = localPath;
            this.approxSize = approxSize;
        }
    }

    private static void add(List<Entry> list, String remotePath, String localPath, long approxSize) {
        list.add(new Entry(remotePath, localPath, approxSize));
    }

    // Minimum set of files required to run ezQuake with maps, skins,
    // crosshairs, skyboxes and core QuakeWorld/nQuake assets: 399 files,
    // ~124.3MB total.
    public static List<Entry> essential() {
        List<Entry> list = new ArrayList<>(399);

        // --- qsw106/ID1 -- original id Software Quake 1.06 shareware ---
        add(list, "qsw106/ID1/PAK0.PAK", "id1/pak0.pak", 18689235L);

        // --- gpl/ezquake -- ezQuake's own GPL assets ---
        add(list, "gpl/ezquake/ezquake.pk3", "ezquake/ezquake.pk3", 6351719L);

        // --- gpl/id1 -- GPL-licensed replacement maps ---
        add(list, "gpl/id1/gpl_maps.pk3", "id1/gpl_maps.pk3", 3154863L);

        // --- gpl/qw -- KTX mod (GPL) ---
        add(list, "gpl/qw/ktx.pk3", "qw/ktx.pk3", 2363039L);

        // --- gpl/qw/skins -- base player skins (GPL) ---
        add(list, "gpl/qw/skins/player_base.png", "qw/skins/player_base.png", 704497L);
        add(list, "gpl/qw/skins/player_blue.png", "qw/skins/player_blue.png", 639819L);
        add(list, "gpl/qw/skins/player_cyan.png", "qw/skins/player_cyan.png", 663323L);
        add(list, "gpl/qw/skins/player_green.png", "qw/skins/player_green.png", 640072L);
        add(list, "gpl/qw/skins/player_orange.png", "qw/skins/player_orange.png", 672544L);
        add(list, "gpl/qw/skins/player_pink.png", "qw/skins/player_pink.png", 684637L);
        add(list, "gpl/qw/skins/player_purple.png", "qw/skins/player_purple.png", 705488L);
        add(list, "gpl/qw/skins/player_red.png", "qw/skins/player_red.png", 639433L);
        add(list, "gpl/qw/skins/player_white.png", "qw/skins/player_white.png", 681074L);
        add(list, "gpl/qw/skins/player_yellow.png", "qw/skins/player_yellow.png", 661036L);

        // --- non-gpl/qw/crosshairs -- crosshair images ---
        add(list, "non-gpl/qw/crosshairs/1.tga", "qw/crosshairs/1.tga", 4140L);
        add(list, "non-gpl/qw/crosshairs/10.tga", "qw/crosshairs/10.tga", 4140L);
        add(list, "non-gpl/qw/crosshairs/11.png", "qw/crosshairs/11.png", 739L);
        add(list, "non-gpl/qw/crosshairs/12.png", "qw/crosshairs/12.png", 329L);
        add(list, "non-gpl/qw/crosshairs/13.png", "qw/crosshairs/13.png", 1433L);
        add(list, "non-gpl/qw/crosshairs/14.png", "qw/crosshairs/14.png", 388L);
        add(list, "non-gpl/qw/crosshairs/15.tga", "qw/crosshairs/15.tga", 23152L);
        add(list, "non-gpl/qw/crosshairs/16.png", "qw/crosshairs/16.png", 389L);
        add(list, "non-gpl/qw/crosshairs/17.tga", "qw/crosshairs/17.tga", 4140L);
        add(list, "non-gpl/qw/crosshairs/18.png", "qw/crosshairs/18.png", 946L);
        add(list, "non-gpl/qw/crosshairs/19.png", "qw/crosshairs/19.png", 433L);
        add(list, "non-gpl/qw/crosshairs/2.png", "qw/crosshairs/2.png", 809L);
        add(list, "non-gpl/qw/crosshairs/20.png", "qw/crosshairs/20.png", 306L);
        add(list, "non-gpl/qw/crosshairs/3.png", "qw/crosshairs/3.png", 420L);
        add(list, "non-gpl/qw/crosshairs/4.tga", "qw/crosshairs/4.tga", 4140L);
        add(list, "non-gpl/qw/crosshairs/5.png", "qw/crosshairs/5.png", 384L);
        add(list, "non-gpl/qw/crosshairs/6.png", "qw/crosshairs/6.png", 406L);
        add(list, "non-gpl/qw/crosshairs/7.png", "qw/crosshairs/7.png", 344L);
        add(list, "non-gpl/qw/crosshairs/8.tga", "qw/crosshairs/8.tga", 4140L);
        add(list, "non-gpl/qw/crosshairs/9.png", "qw/crosshairs/9.png", 361L);

        // --- non-gpl/qw/env -- skyboxes ---
        add(list, "non-gpl/qw/env/doombk.png", "qw/env/doombk.png", 200865L);
        add(list, "non-gpl/qw/env/doomdn.png", "qw/env/doomdn.png", 136549L);
        add(list, "non-gpl/qw/env/doomft.png", "qw/env/doomft.png", 248450L);
        add(list, "non-gpl/qw/env/doomlf.png", "qw/env/doomlf.png", 228652L);
        add(list, "non-gpl/qw/env/doomrt.png", "qw/env/doomrt.png", 246807L);
        add(list, "non-gpl/qw/env/doomup.png", "qw/env/doomup.png", 154005L);
        add(list, "non-gpl/qw/env/grimmnight_bk.jpg", "qw/env/grimmnight_bk.jpg", 29624L);
        add(list, "non-gpl/qw/env/grimmnight_dn.jpg", "qw/env/grimmnight_dn.jpg", 13412L);
        add(list, "non-gpl/qw/env/grimmnight_ft.jpg", "qw/env/grimmnight_ft.jpg", 30422L);
        add(list, "non-gpl/qw/env/grimmnight_lf.jpg", "qw/env/grimmnight_lf.jpg", 27579L);
        add(list, "non-gpl/qw/env/grimmnight_rt.jpg", "qw/env/grimmnight_rt.jpg", 35747L);
        add(list, "non-gpl/qw/env/grimmnight_up.jpg", "qw/env/grimmnight_up.jpg", 18152L);
        add(list, "non-gpl/qw/env/interstellar_bk.jpg", "qw/env/interstellar_bk.jpg", 221975L);
        add(list, "non-gpl/qw/env/interstellar_dn.jpg", "qw/env/interstellar_dn.jpg", 21922L);
        add(list, "non-gpl/qw/env/interstellar_ft.jpg", "qw/env/interstellar_ft.jpg", 218651L);
        add(list, "non-gpl/qw/env/interstellar_lf.jpg", "qw/env/interstellar_lf.jpg", 214447L);
        add(list, "non-gpl/qw/env/interstellar_rt.jpg", "qw/env/interstellar_rt.jpg", 214295L);
        add(list, "non-gpl/qw/env/interstellar_up.jpg", "qw/env/interstellar_up.jpg", 432634L);
        add(list, "non-gpl/qw/env/miramar_bk.jpg", "qw/env/miramar_bk.jpg", 68399L);
        add(list, "non-gpl/qw/env/miramar_dn.jpg", "qw/env/miramar_dn.jpg", 13415L);
        add(list, "non-gpl/qw/env/miramar_ft.jpg", "qw/env/miramar_ft.jpg", 66232L);
        add(list, "non-gpl/qw/env/miramar_lf.jpg", "qw/env/miramar_lf.jpg", 52728L);
        add(list, "non-gpl/qw/env/miramar_rt.jpg", "qw/env/miramar_rt.jpg", 58561L);
        add(list, "non-gpl/qw/env/miramar_up.jpg", "qw/env/miramar_up.jpg", 32579L);
        add(list, "non-gpl/qw/env/readme.txt", "qw/env/readme.txt", 100L);
        add(list, "non-gpl/qw/env/space_bk.jpg", "qw/env/space_bk.jpg", 114902L);
        add(list, "non-gpl/qw/env/space_dn.jpg", "qw/env/space_dn.jpg", 47920L);
        add(list, "non-gpl/qw/env/space_ft.jpg", "qw/env/space_ft.jpg", 88616L);
        add(list, "non-gpl/qw/env/space_lf.jpg", "qw/env/space_lf.jpg", 70469L);
        add(list, "non-gpl/qw/env/space_rt.jpg", "qw/env/space_rt.jpg", 66767L);
        add(list, "non-gpl/qw/env/space_up.jpg", "qw/env/space_up.jpg", 63320L);
        add(list, "non-gpl/qw/env/stormydays_bk.jpg", "qw/env/stormydays_bk.jpg", 66839L);
        add(list, "non-gpl/qw/env/stormydays_dn.jpg", "qw/env/stormydays_dn.jpg", 61630L);
        add(list, "non-gpl/qw/env/stormydays_ft.jpg", "qw/env/stormydays_ft.jpg", 74093L);
        add(list, "non-gpl/qw/env/stormydays_lf.jpg", "qw/env/stormydays_lf.jpg", 66591L);
        add(list, "non-gpl/qw/env/stormydays_rt.jpg", "qw/env/stormydays_rt.jpg", 72273L);
        add(list, "non-gpl/qw/env/stormydays_up.jpg", "qw/env/stormydays_up.jpg", 26168L);
        add(list, "non-gpl/qw/env/violentdays_bk.jpg", "qw/env/violentdays_bk.jpg", 63110L);
        add(list, "non-gpl/qw/env/violentdays_dn.jpg", "qw/env/violentdays_dn.jpg", 81877L);
        add(list, "non-gpl/qw/env/violentdays_ft.jpg", "qw/env/violentdays_ft.jpg", 81073L);
        add(list, "non-gpl/qw/env/violentdays_lf.jpg", "qw/env/violentdays_lf.jpg", 62620L);
        add(list, "non-gpl/qw/env/violentdays_rt.jpg", "qw/env/violentdays_rt.jpg", 78775L);
        add(list, "non-gpl/qw/env/violentdays_up.jpg", "qw/env/violentdays_up.jpg", 28945L);

        // --- non-gpl/qw/maps -- deathmatch maps (.bsp/.txt/.ent) ---
        add(list, "non-gpl/qw/maps/a2.bsp", "qw/maps/a2.bsp", 884992L);
        add(list, "non-gpl/qw/maps/a2.txt", "qw/maps/a2.txt", 3100L);
        add(list, "non-gpl/qw/maps/aerowalk.bsp", "qw/maps/aerowalk.bsp", 632040L);
        add(list, "non-gpl/qw/maps/aerowalk.txt", "qw/maps/aerowalk.txt", 1307L);
        add(list, "non-gpl/qw/maps/amphi.bsp", "qw/maps/amphi.bsp", 120844L);
        add(list, "non-gpl/qw/maps/arena3.ent", "qw/maps/arena3.ent", 13012L);
        add(list, "non-gpl/qw/maps/arena5.ent", "qw/maps/arena5.ent", 17959L);
        add(list, "non-gpl/qw/maps/barrel.ent", "qw/maps/barrel.ent", 20402L);
        add(list, "non-gpl/qw/maps/bloodfest.ent", "qw/maps/bloodfest.ent", 17073L);
        add(list, "non-gpl/qw/maps/bravado.bsp", "qw/maps/bravado.bsp", 839536L);
        add(list, "non-gpl/qw/maps/cmt1b.bsp", "qw/maps/cmt1b.bsp", 1970300L);
        add(list, "non-gpl/qw/maps/cmt3.bsp", "qw/maps/cmt3.bsp", 1567920L);
        add(list, "non-gpl/qw/maps/cmt4.bsp", "qw/maps/cmt4.bsp", 882788L);
        add(list, "non-gpl/qw/maps/cmt4.txt", "qw/maps/cmt4.txt", 3247L);
        add(list, "non-gpl/qw/maps/cmt5b.bsp", "qw/maps/cmt5b.bsp", 1294048L);
        add(list, "non-gpl/qw/maps/cpm1qw.bsp", "qw/maps/cpm1qw.bsp", 948896L);
        add(list, "non-gpl/qw/maps/cpm1qw.txt", "qw/maps/cpm1qw.txt", 2199L);
        add(list, "non-gpl/qw/maps/cpm3a.bsp", "qw/maps/cpm3a.bsp", 2643884L);
        add(list, "non-gpl/qw/maps/cpm3qw.bsp", "qw/maps/cpm3qw.bsp", 373972L);
        add(list, "non-gpl/qw/maps/death32c.bsp", "qw/maps/death32c.bsp", 1922656L);
        add(list, "non-gpl/qw/maps/death6.ent", "qw/maps/death6.ent", 24286L);
        add(list, "non-gpl/qw/maps/debello.bsp", "qw/maps/debello.bsp", 821220L);
        add(list, "non-gpl/qw/maps/debello.txt", "qw/maps/debello.txt", 4143L);
        add(list, "non-gpl/qw/maps/del1.bsp", "qw/maps/del1.bsp", 430276L);
        add(list, "non-gpl/qw/maps/dm2dmm4.bsp", "qw/maps/dm2dmm4.bsp", 518332L);
        add(list, "non-gpl/qw/maps/dm3hill.bsp", "qw/maps/dm3hill.bsp", 335104L);
        add(list, "non-gpl/qw/maps/dm4ish.bsp", "qw/maps/dm4ish.bsp", 400004L);
        add(list, "non-gpl/qw/maps/dm4ish.ent", "qw/maps/dm4ish.ent", 16047L);
        add(list, "non-gpl/qw/maps/dm4ish.txt", "qw/maps/dm4ish.txt", 2309L);
        add(list, "non-gpl/qw/maps/dm5a.bsp", "qw/maps/dm5a.bsp", 476832L);
        add(list, "non-gpl/qw/maps/e1m7.ent", "qw/maps/e1m7.ent", 22479L);
        add(list, "non-gpl/qw/maps/endif.bsp", "qw/maps/endif.bsp", 236856L);
        add(list, "non-gpl/qw/maps/fmc.bsp", "qw/maps/fmc.bsp", 566336L);
        add(list, "non-gpl/qw/maps/fmc.txt", "qw/maps/fmc.txt", 2297L);
        add(list, "non-gpl/qw/maps/four.bsp", "qw/maps/four.bsp", 888380L);
        add(list, "non-gpl/qw/maps/four.txt", "qw/maps/four.txt", 5367L);
        add(list, "non-gpl/qw/maps/fragyard.ent", "qw/maps/fragyard.ent", 23195L);
        add(list, "non-gpl/qw/maps/frobodm2.bsp", "qw/maps/frobodm2.bsp", 846688L);
        add(list, "non-gpl/qw/maps/frobodm2.txt", "qw/maps/frobodm2.txt", 945L);
        add(list, "non-gpl/qw/maps/genocide.bsp", "qw/maps/genocide.bsp", 818756L);
        add(list, "non-gpl/qw/maps/genocide.ent", "qw/maps/genocide.ent", 15396L);
        add(list, "non-gpl/qw/maps/genocide.txt", "qw/maps/genocide.txt", 2022L);
        add(list, "non-gpl/qw/maps/hate.bsp", "qw/maps/hate.bsp", 826980L);
        add(list, "non-gpl/qw/maps/hate.txt", "qw/maps/hate.txt", 2296L);
        add(list, "non-gpl/qw/maps/hohoho.ent", "qw/maps/hohoho.ent", 14796L);
        add(list, "non-gpl/qw/maps/hook.txt", "qw/maps/hook.txt", 5637L);
        add(list, "non-gpl/qw/maps/kenya.ent", "qw/maps/kenya.ent", 17215L);
        add(list, "non-gpl/qw/maps/kjdm7.bsp", "qw/maps/kjdm7.bsp", 277940L);
        add(list, "non-gpl/qw/maps/kjdm7.txt", "qw/maps/kjdm7.txt", 2042L);
        add(list, "non-gpl/qw/maps/lady.bsp", "qw/maps/lady.bsp", 809996L);
        add(list, "non-gpl/qw/maps/leaks.bsp", "qw/maps/leaks.bsp", 835224L);
        add(list, "non-gpl/qw/maps/leaks.txt", "qw/maps/leaks.txt", 1600L);
        add(list, "non-gpl/qw/maps/monsoon.bsp", "qw/maps/monsoon.bsp", 1502400L);
        add(list, "non-gpl/qw/maps/oldcrat.bsp", "qw/maps/oldcrat.bsp", 199980L);
        add(list, "non-gpl/qw/maps/oldcrat.txt", "qw/maps/oldcrat.txt", 1511L);
        add(list, "non-gpl/qw/maps/pdm18.bsp", "qw/maps/pdm18.bsp", 267812L);
        add(list, "non-gpl/qw/maps/pillar.ent", "qw/maps/pillar.ent", 16845L);
        add(list, "non-gpl/qw/maps/pkeg.bsp", "qw/maps/pkeg.bsp", 461120L);
        add(list, "non-gpl/qw/maps/pkeg1.bsp", "qw/maps/pkeg1.bsp", 363672L);
        add(list, "non-gpl/qw/maps/pkeg1.txt", "qw/maps/pkeg1.txt", 9471L);
        add(list, "non-gpl/qw/maps/povdmm4.bsp", "qw/maps/povdmm4.bsp", 130920L);
        add(list, "non-gpl/qw/maps/povdmm4.txt", "qw/maps/povdmm4.txt", 273L);
        add(list, "non-gpl/qw/maps/pushdmm4.bsp", "qw/maps/pushdmm4.bsp", 131108L);
        add(list, "non-gpl/qw/maps/pushdmm4.txt", "qw/maps/pushdmm4.txt", 1814L);
        add(list, "non-gpl/qw/maps/q1dm17.ent", "qw/maps/q1dm17.ent", 21135L);
        add(list, "non-gpl/qw/maps/q3dm6ish.bsp", "qw/maps/q3dm6ish.bsp", 490628L);
        add(list, "non-gpl/qw/maps/q3dm6ish.txt", "qw/maps/q3dm6ish.txt", 3236L);
        add(list, "non-gpl/qw/maps/q3dm6qw.bsp", "qw/maps/q3dm6qw.bsp", 2740036L);
        add(list, "non-gpl/qw/maps/qcon1.bsp", "qw/maps/qcon1.bsp", 376776L);
        add(list, "non-gpl/qw/maps/qcon1.txt", "qw/maps/qcon1.txt", 1813L);
        add(list, "non-gpl/qw/maps/rwild.bsp", "qw/maps/rwild.bsp", 579164L);
        add(list, "non-gpl/qw/maps/rz1pondb.ent", "qw/maps/rz1pondb.ent", 14898L);
        add(list, "non-gpl/qw/maps/sacredb1.bsp", "qw/maps/sacredb1.bsp", 1420464L);
        add(list, "non-gpl/qw/maps/schduel.bsp", "qw/maps/schduel.bsp", 1694564L);
        add(list, "non-gpl/qw/maps/schloss.bsp", "qw/maps/schloss.bsp", 2197772L);
        add(list, "non-gpl/qw/maps/shine.bsp", "qw/maps/shine.bsp", 695332L);
        add(list, "non-gpl/qw/maps/skull.bsp", "qw/maps/skull.bsp", 1117204L);
        add(list, "non-gpl/qw/maps/slaug.ent", "qw/maps/slaug.ent", 13809L);
        add(list, "non-gpl/qw/maps/slide1.bsp", "qw/maps/slide1.bsp", 1163476L);
        add(list, "non-gpl/qw/maps/slide1.txt", "qw/maps/slide1.txt", 2278L);
        add(list, "non-gpl/qw/maps/slide7.bsp", "qw/maps/slide7.bsp", 1608580L);
        add(list, "non-gpl/qw/maps/slide7.txt", "qw/maps/slide7.txt", 2278L);
        add(list, "non-gpl/qw/maps/speed.bsp", "qw/maps/speed.bsp", 522324L);
        add(list, "non-gpl/qw/maps/spinev2.bsp", "qw/maps/spinev2.bsp", 472072L);
        add(list, "non-gpl/qw/maps/spinev2.txt", "qw/maps/spinev2.txt", 2176L);
        add(list, "non-gpl/qw/maps/subterfuge.bsp", "qw/maps/subterfuge.bsp", 1425016L);
        add(list, "non-gpl/qw/maps/subterfuge.txt", "qw/maps/subterfuge.txt", 5361L);
        add(list, "non-gpl/qw/maps/travelert6.bsp", "qw/maps/travelert6.bsp", 638444L);
        add(list, "non-gpl/qw/maps/tridm3.bsp", "qw/maps/tridm3.bsp", 570852L);
        add(list, "non-gpl/qw/maps/ukpak2.bsp", "qw/maps/ukpak2.bsp", 655480L);
        add(list, "non-gpl/qw/maps/ukpak2.txt", "qw/maps/ukpak2.txt", 4535L);
        add(list, "non-gpl/qw/maps/ultrav.bsp", "qw/maps/ultrav.bsp", 609868L);
        add(list, "non-gpl/qw/maps/ultrav.txt", "qw/maps/ultrav.txt", 3554L);
        add(list, "non-gpl/qw/maps/vdm3v3.bsp", "qw/maps/vdm3v3.bsp", 818940L);
        add(list, "non-gpl/qw/maps/way2ez.bsp", "qw/maps/way2ez.bsp", 442704L);
        add(list, "non-gpl/qw/maps/way2ez2.bsp", "qw/maps/way2ez2.bsp", 1239024L);
        add(list, "non-gpl/qw/maps/ztndm3.bsp", "qw/maps/ztndm3.bsp", 394888L);
        add(list, "non-gpl/qw/maps/ztndm3.txt", "qw/maps/ztndm3.txt", 1665L);
        add(list, "non-gpl/qw/maps/ztndm3q.bsp", "qw/maps/ztndm3q.bsp", 435032L);
        add(list, "non-gpl/qw/maps/ztndm4.bsp", "qw/maps/ztndm4.bsp", 402204L);
        add(list, "non-gpl/qw/maps/ztndm4.txt", "qw/maps/ztndm4.txt", 1911L);
        add(list, "non-gpl/qw/maps/ztndm6.bsp", "qw/maps/ztndm6.bsp", 484868L);
        add(list, "non-gpl/qw/maps/ztndm6.txt", "qw/maps/ztndm6.txt", 2039L);
        add(list, "non-gpl/qw/maps/ztrain.bsp", "qw/maps/ztrain.bsp", 375212L);
        add(list, "non-gpl/qw/maps/ztricks.bsp", "qw/maps/ztricks.bsp", 1007968L);
        add(list, "non-gpl/qw/maps/ztricks2.bsp", "qw/maps/ztricks2.bsp", 876312L);

        // --- non-gpl/qw/skins -- extra player skins ---
        add(list, "non-gpl/qw/skins/1.png", "qw/skins/1.png", 6853L);
        add(list, "non-gpl/qw/skins/2.png", "qw/skins/2.png", 6853L);
        add(list, "non-gpl/qw/skins/2_blue.png", "qw/skins/2_blue.png", 334686L);
        add(list, "non-gpl/qw/skins/2_cyan.png", "qw/skins/2_cyan.png", 356800L);
        add(list, "non-gpl/qw/skins/2_green.png", "qw/skins/2_green.png", 334678L);
        add(list, "non-gpl/qw/skins/2_pink.png", "qw/skins/2_pink.png", 420517L);
        add(list, "non-gpl/qw/skins/2_red.png", "qw/skins/2_red.png", 334506L);
        add(list, "non-gpl/qw/skins/2_white.png", "qw/skins/2_white.png", 394940L);
        add(list, "non-gpl/qw/skins/2_yellow.png", "qw/skins/2_yellow.png", 356696L);
        add(list, "non-gpl/qw/skins/3.png", "qw/skins/3.png", 6853L);
        add(list, "non-gpl/qw/skins/4.png", "qw/skins/4.png", 6698L);
        add(list, "non-gpl/qw/skins/5.png", "qw/skins/5.png", 8747L);
        add(list, "non-gpl/qw/skins/6.png", "qw/skins/6.png", 6853L);
        add(list, "non-gpl/qw/skins/7.png", "qw/skins/7.png", 6853L);
        add(list, "non-gpl/qw/skins/8.png", "qw/skins/8.png", 8789L);
        add(list, "non-gpl/qw/skins/9.png", "qw/skins/9.png", 8788L);
        add(list, "non-gpl/qw/skins/base.png", "qw/skins/base.png", 608079L);
        add(list, "non-gpl/qw/skins/blue.png", "qw/skins/blue.png", 7337L);
        add(list, "non-gpl/qw/skins/cyan.png", "qw/skins/cyan.png", 3221L);
        add(list, "non-gpl/qw/skins/green.png", "qw/skins/green.png", 12921L);
        add(list, "non-gpl/qw/skins/helmet.png", "qw/skins/helmet.png", 583518L);
        add(list, "non-gpl/qw/skins/lightblue.png", "qw/skins/lightblue.png", 7153L);
        add(list, "non-gpl/qw/skins/orange.png", "qw/skins/orange.png", 10231L);
        add(list, "non-gpl/qw/skins/pink.png", "qw/skins/pink.png", 5926L);
        add(list, "non-gpl/qw/skins/player_azure.png", "qw/skins/player_azure.png", 510905L);
        add(list, "non-gpl/qw/skins/player_base.png", "qw/skins/player_base.png", 704497L);
        add(list, "non-gpl/qw/skins/player_blue.png", "qw/skins/player_blue.png", 639819L);
        add(list, "non-gpl/qw/skins/player_crimson.png", "qw/skins/player_crimson.png", 524903L);
        add(list, "non-gpl/qw/skins/player_cyan.png", "qw/skins/player_cyan.png", 663323L);
        add(list, "non-gpl/qw/skins/player_green.png", "qw/skins/player_green.png", 640072L);
        add(list, "non-gpl/qw/skins/player_lgtgreen.png", "qw/skins/player_lgtgreen.png", 497628L);
        add(list, "non-gpl/qw/skins/player_lime.png", "qw/skins/player_lime.png", 521343L);
        add(list, "non-gpl/qw/skins/player_mayablue.png", "qw/skins/player_mayablue.png", 555876L);
        add(list, "non-gpl/qw/skins/player_orange.png", "qw/skins/player_orange.png", 507502L);
        add(list, "non-gpl/qw/skins/player_pink.png", "qw/skins/player_pink.png", 684637L);
        add(list, "non-gpl/qw/skins/player_pumpkin.png", "qw/skins/player_pumpkin.png", 534000L);
        add(list, "non-gpl/qw/skins/player_purple.png", "qw/skins/player_purple.png", 705488L);
        add(list, "non-gpl/qw/skins/player_red.png", "qw/skins/player_red.png", 639433L);
        add(list, "non-gpl/qw/skins/player_rose.png", "qw/skins/player_rose.png", 512718L);
        add(list, "non-gpl/qw/skins/player_silver.png", "qw/skins/player_silver.png", 522694L);
        add(list, "non-gpl/qw/skins/player_white.png", "qw/skins/player_white.png", 681074L);
        add(list, "non-gpl/qw/skins/player_yellow.png", "qw/skins/player_yellow.png", 661036L);
        add(list, "non-gpl/qw/skins/red.png", "qw/skins/red.png", 8398L);
        add(list, "non-gpl/qw/skins/t1.png", "qw/skins/t1.png", 685L);
        add(list, "non-gpl/qw/skins/t2.png", "qw/skins/t2.png", 644L);
        add(list, "non-gpl/qw/skins/t3.png", "qw/skins/t3.png", 644L);
        add(list, "non-gpl/qw/skins/t4.png", "qw/skins/t4.png", 639L);
        add(list, "non-gpl/qw/skins/t5.png", "qw/skins/t5.png", 638L);
        add(list, "non-gpl/qw/skins/white.png", "qw/skins/white.png", 3233L);
        add(list, "non-gpl/qw/skins/yellow.png", "qw/skins/yellow.png", 14782L);

        // --- non-gpl/qw/textures -- base textures (bmodels/charsets/models/wad) ---
        add(list, "non-gpl/qw/textures/bmodels/simple_b_batt0_0.tga", "qw/textures/bmodels/simple_b_batt0_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_batt1_0.tga", "qw/textures/bmodels/simple_b_batt1_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_bh100_0.tga", "qw/textures/bmodels/simple_b_bh100_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_bh10_0.tga", "qw/textures/bmodels/simple_b_bh10_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_bh25_0.tga", "qw/textures/bmodels/simple_b_bh25_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_nail0_0.tga", "qw/textures/bmodels/simple_b_nail0_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_nail1_0.tga", "qw/textures/bmodels/simple_b_nail1_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_rock0_0.tga", "qw/textures/bmodels/simple_b_rock0_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_rock1_0.tga", "qw/textures/bmodels/simple_b_rock1_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_shell0_0.tga", "qw/textures/bmodels/simple_b_shell0_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/bmodels/simple_b_shell1_0.tga", "qw/textures/bmodels/simple_b_shell1_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/charsets/1.png", "qw/textures/charsets/1.png", 214312L);
        add(list, "non-gpl/qw/textures/charsets/10.png", "qw/textures/charsets/10.png", 126462L);
        add(list, "non-gpl/qw/textures/charsets/11.png", "qw/textures/charsets/11.png", 200869L);
        add(list, "non-gpl/qw/textures/charsets/12.png", "qw/textures/charsets/12.png", 167372L);
        add(list, "non-gpl/qw/textures/charsets/13.png", "qw/textures/charsets/13.png", 66296L);
        add(list, "non-gpl/qw/textures/charsets/14.png", "qw/textures/charsets/14.png", 21225L);
        add(list, "non-gpl/qw/textures/charsets/15.png", "qw/textures/charsets/15.png", 40068L);
        add(list, "non-gpl/qw/textures/charsets/16.png", "qw/textures/charsets/16.png", 146568L);
        add(list, "non-gpl/qw/textures/charsets/17.png", "qw/textures/charsets/17.png", 240873L);
        add(list, "non-gpl/qw/textures/charsets/18.png", "qw/textures/charsets/18.png", 203577L);
        add(list, "non-gpl/qw/textures/charsets/19.png", "qw/textures/charsets/19.png", 113513L);
        add(list, "non-gpl/qw/textures/charsets/1_fixed.png", "qw/textures/charsets/1_fixed.png", 237472L);
        add(list, "non-gpl/qw/textures/charsets/2.png", "qw/textures/charsets/2.png", 187426L);
        add(list, "non-gpl/qw/textures/charsets/20.png", "qw/textures/charsets/20.png", 187765L);
        add(list, "non-gpl/qw/textures/charsets/3.png", "qw/textures/charsets/3.png", 320315L);
        add(list, "non-gpl/qw/textures/charsets/4.png", "qw/textures/charsets/4.png", 166817L);
        add(list, "non-gpl/qw/textures/charsets/5.png", "qw/textures/charsets/5.png", 140913L);
        add(list, "non-gpl/qw/textures/charsets/6.png", "qw/textures/charsets/6.png", 18263L);
        add(list, "non-gpl/qw/textures/charsets/7.png", "qw/textures/charsets/7.png", 41363L);
        add(list, "non-gpl/qw/textures/charsets/8.png", "qw/textures/charsets/8.png", 7539L);
        add(list, "non-gpl/qw/textures/charsets/9.png", "qw/textures/charsets/9.png", 19012L);
        add(list, "non-gpl/qw/textures/models/flag_0.png", "qw/textures/models/flag_0.png", 147952L);
        add(list, "non-gpl/qw/textures/models/flag_1.png", "qw/textures/models/flag_1.png", 166005L);
        add(list, "non-gpl/qw/textures/models/simple_armor_0.tga", "qw/textures/models/simple_armor_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_armor_1.tga", "qw/textures/models/simple_armor_1.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_armor_2.tga", "qw/textures/models/simple_armor_2.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_light_0.tga", "qw/textures/models/simple_g_light_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_nail2_0.tga", "qw/textures/models/simple_g_nail2_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_nail_0.tga", "qw/textures/models/simple_g_nail_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_rock2_0.tga", "qw/textures/models/simple_g_rock2_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_rock_0.tga", "qw/textures/models/simple_g_rock_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_g_shot_0.tga", "qw/textures/models/simple_g_shot_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_invisibl_0.tga", "qw/textures/models/simple_invisibl_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_invulner_0.tga", "qw/textures/models/simple_invulner_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_quaddama_0.tga", "qw/textures/models/simple_quaddama_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/models/simple_suit_0.tga", "qw/textures/models/simple_suit_0.tga", 65580L);
        add(list, "non-gpl/qw/textures/wad/anum_0.tga", "qw/textures/wad/anum_0.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_1.tga", "qw/textures/wad/anum_1.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_2.tga", "qw/textures/wad/anum_2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_3.tga", "qw/textures/wad/anum_3.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_4.tga", "qw/textures/wad/anum_4.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_5.tga", "qw/textures/wad/anum_5.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_6.tga", "qw/textures/wad/anum_6.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_7.tga", "qw/textures/wad/anum_7.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_8.tga", "qw/textures/wad/anum_8.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_9.tga", "qw/textures/wad/anum_9.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_colon.tga", "qw/textures/wad/anum_colon.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/anum_minus.tga", "qw/textures/wad/anum_minus.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/anum_slash.png", "qw/textures/wad/anum_slash.png", 1517L);
        add(list, "non-gpl/qw/textures/wad/disc.tga", "qw/textures/wad/disc.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face1.tga", "qw/textures/wad/face1.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face2.tga", "qw/textures/wad/face2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face3.tga", "qw/textures/wad/face3.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face4.tga", "qw/textures/wad/face4.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face5.tga", "qw/textures/wad/face5.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_inv2.png", "qw/textures/wad/face_inv2.png", 5207L);
        add(list, "non-gpl/qw/textures/wad/face_inv2.tga", "qw/textures/wad/face_inv2.tga", 10032L);
        add(list, "non-gpl/qw/textures/wad/face_invis.tga", "qw/textures/wad/face_invis.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_invul2.tga", "qw/textures/wad/face_invul2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_p1.tga", "qw/textures/wad/face_p1.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_p2.tga", "qw/textures/wad/face_p2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_p3.tga", "qw/textures/wad/face_p3.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_p4.tga", "qw/textures/wad/face_p4.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_p5.tga", "qw/textures/wad/face_p5.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/face_quad.tga", "qw/textures/wad/face_quad.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/ibar.png", "qw/textures/wad/ibar.png", 6174L);
        add(list, "non-gpl/qw/textures/wad/inv2_lightng.tga", "qw/textures/wad/inv2_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inv2_nailgun.tga", "qw/textures/wad/inv2_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv2_rlaunch.tga", "qw/textures/wad/inv2_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv2_shotgun.tga", "qw/textures/wad/inv2_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv2_snailgun.tga", "qw/textures/wad/inv2_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv2_srlaunch.tga", "qw/textures/wad/inv2_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv2_sshotgun.tga", "qw/textures/wad/inv2_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_lightng.tga", "qw/textures/wad/inv_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inv_nailgun.tga", "qw/textures/wad/inv_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_rlaunch.tga", "qw/textures/wad/inv_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_shotgun.tga", "qw/textures/wad/inv_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_snailgun.tga", "qw/textures/wad/inv_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_srlaunch.tga", "qw/textures/wad/inv_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inv_sshotgun.tga", "qw/textures/wad/inv_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_lightng.tga", "qw/textures/wad/inva1_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inva1_nailgun.tga", "qw/textures/wad/inva1_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_rlaunch.tga", "qw/textures/wad/inva1_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_shotgun.tga", "qw/textures/wad/inva1_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_snailgun.tga", "qw/textures/wad/inva1_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_srlaunch.tga", "qw/textures/wad/inva1_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva1_sshotgun.tga", "qw/textures/wad/inva1_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_lightng.tga", "qw/textures/wad/inva2_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inva2_nailgun.tga", "qw/textures/wad/inva2_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_rlaunch.tga", "qw/textures/wad/inva2_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_shotgun.tga", "qw/textures/wad/inva2_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_snailgun.tga", "qw/textures/wad/inva2_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_srlaunch.tga", "qw/textures/wad/inva2_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva2_sshotgun.tga", "qw/textures/wad/inva2_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_lightng.tga", "qw/textures/wad/inva3_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inva3_nailgun.tga", "qw/textures/wad/inva3_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_rlaunch.tga", "qw/textures/wad/inva3_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_shotgun.tga", "qw/textures/wad/inva3_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_snailgun.tga", "qw/textures/wad/inva3_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_srlaunch.tga", "qw/textures/wad/inva3_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva3_sshotgun.tga", "qw/textures/wad/inva3_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_lightng.tga", "qw/textures/wad/inva4_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inva4_nailgun.tga", "qw/textures/wad/inva4_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_rlaunch.tga", "qw/textures/wad/inva4_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_shotgun.tga", "qw/textures/wad/inva4_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_snailgun.tga", "qw/textures/wad/inva4_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_srlaunch.tga", "qw/textures/wad/inva4_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva4_sshotgun.tga", "qw/textures/wad/inva4_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_lightng.tga", "qw/textures/wad/inva5_lightng.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/inva5_nailgun.tga", "qw/textures/wad/inva5_nailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_rlaunch.tga", "qw/textures/wad/inva5_rlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_shotgun.tga", "qw/textures/wad/inva5_shotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_snailgun.tga", "qw/textures/wad/inva5_snailgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_srlaunch.tga", "qw/textures/wad/inva5_srlaunch.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/inva5_sshotgun.tga", "qw/textures/wad/inva5_sshotgun.tga", 24620L);
        add(list, "non-gpl/qw/textures/wad/net.png", "qw/textures/wad/net.png", 3944L);
        add(list, "non-gpl/qw/textures/wad/num_0.tga", "qw/textures/wad/num_0.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_1.tga", "qw/textures/wad/num_1.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_2.tga", "qw/textures/wad/num_2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_3.tga", "qw/textures/wad/num_3.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_4.tga", "qw/textures/wad/num_4.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_5.tga", "qw/textures/wad/num_5.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_6.tga", "qw/textures/wad/num_6.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_7.tga", "qw/textures/wad/num_7.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_8.tga", "qw/textures/wad/num_8.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_9.tga", "qw/textures/wad/num_9.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_colon.tga", "qw/textures/wad/num_colon.tga", 49196L);
        add(list, "non-gpl/qw/textures/wad/num_minus.tga", "qw/textures/wad/num_minus.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/num_slash.png", "qw/textures/wad/num_slash.png", 1419L);
        add(list, "non-gpl/qw/textures/wad/sb_armor1.tga", "qw/textures/wad/sb_armor1.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/sb_armor2.tga", "qw/textures/wad/sb_armor2.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/sb_armor3.tga", "qw/textures/wad/sb_armor3.tga", 36908L);
        add(list, "non-gpl/qw/textures/wad/sb_cells.png", "qw/textures/wad/sb_cells.png", 96883L);
        add(list, "non-gpl/qw/textures/wad/sb_invis.tga", "qw/textures/wad/sb_invis.tga", 16428L);
        add(list, "non-gpl/qw/textures/wad/sb_invuln.tga", "qw/textures/wad/sb_invuln.tga", 16428L);
        add(list, "non-gpl/qw/textures/wad/sb_key1.png", "qw/textures/wad/sb_key1.png", 7119L);
        add(list, "non-gpl/qw/textures/wad/sb_key2.png", "qw/textures/wad/sb_key2.png", 7532L);
        add(list, "non-gpl/qw/textures/wad/sb_nails.png", "qw/textures/wad/sb_nails.png", 91282L);
        add(list, "non-gpl/qw/textures/wad/sb_quad.tga", "qw/textures/wad/sb_quad.tga", 16428L);
        add(list, "non-gpl/qw/textures/wad/sb_rocket.png", "qw/textures/wad/sb_rocket.png", 53868L);
        add(list, "non-gpl/qw/textures/wad/sb_shells.png", "qw/textures/wad/sb_shells.png", 96802L);
        add(list, "non-gpl/qw/textures/wad/sb_sigil1.png", "qw/textures/wad/sb_sigil1.png", 2769L);
        add(list, "non-gpl/qw/textures/wad/sb_sigil2.png", "qw/textures/wad/sb_sigil2.png", 3275L);
        add(list, "non-gpl/qw/textures/wad/sb_sigil3.png", "qw/textures/wad/sb_sigil3.png", 2342L);
        add(list, "non-gpl/qw/textures/wad/sb_sigil4.png", "qw/textures/wad/sb_sigil4.png", 3225L);
        add(list, "non-gpl/qw/textures/wad/sb_suit.tga", "qw/textures/wad/sb_suit.tga", 16428L);
        add(list, "non-gpl/qw/textures/wad/sbar.png", "qw/textures/wad/sbar.png", 357L);
        add(list, "non-gpl/qw/textures/wad/scorebar.png", "qw/textures/wad/scorebar.png", 357L);
        add(list, "non-gpl/qw/textures/wad/turtle.png", "qw/textures/wad/turtle.png", 20276L);

        // --- non-gpl/qw -- configs and core QuakeWorld/nQuake packages ---
        add(list, "non-gpl/qw/autoexec.cfg", "qw/autoexec.cfg", 20325L);
        add(list, "non-gpl/qw/fragfile.dat", "qw/fragfile.dat", 38950L);
        add(list, "non-gpl/qw/models.pk3", "qw/models.pk3", 4051284L);
        add(list, "non-gpl/qw/nquake.pk3", "qw/nquake.pk3", 9467858L);
        add(list, "non-gpl/qw/nquake_default.cfg", "qw/nquake_default.cfg", 80565L);
        add(list, "non-gpl/qw/scoreboard_flags.pk3", "qw/scoreboard_flags.pk3", 379755L);

        // TOTAL: 399 files, 130304546 bytes (124.3 MB)
        return list;
    }

    // Optional HD texture pack (addon-textures/qw in the upstream repo, note
    // the different remote directory from the essential manifest's
    // non-gpl/qw): 5 files, ~385MB total. Offered separately after the
    // essential download completes, never bundled automatically.
    public static List<Entry> hdTextures() {
        List<Entry> list = new ArrayList<>(5);
        add(list, "addon-textures/qw/qrp_maps_textures_1.pk3", "qw/qrp_maps_textures_1.pk3", 102995518L);
        add(list, "addon-textures/qw/qrp_maps_textures_2.pk3", "qw/qrp_maps_textures_2.pk3", 97723334L);
        add(list, "addon-textures/qw/qrp_maps_textures_3.pk3", "qw/qrp_maps_textures_3.pk3", 102378278L);
        add(list, "addon-textures/qw/qrp_maps_textures_4.pk3", "qw/qrp_maps_textures_4.pk3", 84214527L);
        add(list, "addon-textures/qw/qrp_b-models.pk3", "qw/qrp_b-models.pk3", 16764275L);
        // TOTAL: 5 files, 404075932 bytes (385.4 MB)
        return list;
    }

    private GameDataManifest() {
    }
}
