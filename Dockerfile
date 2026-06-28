FROM debian:bookworm-slim AS builder

RUN dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -y \
        crossbuild-essential-armhf \
        libasound2-dev:armhf \
        git make \
    && apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY src/doomgeneric_primego.c .
COPY stub_SDL_mixer.h .
RUN git clone --depth 1 https://github.com/ozkl/doomgeneric.git && \
    mv doomgeneric_primego.c doomgeneric/doomgeneric/ && \
    cp stub_SDL_mixer.h doomgeneric/doomgeneric/SDL_mixer.h && \
    sed -i 's/M_StartMessage(endstring,M_QuitResponse,true);/I_Quit();/' doomgeneric/doomgeneric/m_menu.c

WORKDIR /build/doomgeneric/doomgeneric

RUN arm-linux-gnueabihf-gcc -O2 -flto \
    -DFEATURE_SOUND \
    -DDOOMGENERIC_RESX=640 \
    -DDOOMGENERIC_RESY=400 \
    -I. \
    -o doomprimego \
    doomgeneric_primego.c \
    am_map.c \
    doomgeneric.c \
    d_main.c d_event.c d_items.c d_iwad.c d_loop.c d_mode.c d_net.c \
    doomdef.c doomstat.c dstrings.c dummy.c f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c \
    i_endoom.c i_input.c i_joystick.c i_scale.c i_sound.c i_system.c i_video.c \
    info.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c \
    m_fixed.c m_menu.c m_misc.c m_random.c memio.c \
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c \
    p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c \
    p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c \
    r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c \
    s_sound.c sounds.c \
    st_lib.c st_stuff.c tables.c v_video.c \
    w_checksum.c w_file.c w_file_stdc.c wi_stuff.c w_main.c w_wad.c z_zone.c \
    sha1.c statdump.c \
    -lm -lasound \
    && arm-linux-gnueabihf-strip doomprimego

FROM scratch
COPY --from=builder /build/doomgeneric/doomgeneric/doomprimego /doomprimego
