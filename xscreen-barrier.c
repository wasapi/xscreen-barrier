#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#define MAX_BARRIERS 32

static Display *dpy = NULL;
static Window root;

static PointerBarrier barriers[MAX_BARRIERS];
static int num_barriers = 0;

static int randr_event_base;
static int randr_error_base;

static void destroy_barriers(void)
{
    for (int i = 0; i < num_barriers; i++) {
        XFixesDestroyPointerBarrier(dpy, barriers[i]);
    }

    num_barriers = 0;
}

static void add_horizontal_barrier(int x1, int x2, int y)
{
    if (x1 >= x2 || num_barriers >= MAX_BARRIERS)
        return;

    barriers[num_barriers++] = XFixesCreatePointerBarrier(
        dpy,
        root,
        x1,
        y,
        x2,
        y,
        0,          /* aucune direction autorisée */
        0,
        NULL
    );

    printf("Barrière horizontale : x=%d..%d, y=%d\n",
           x1, x2, y);
}

static void add_vertical_barrier(int y1, int y2, int x)
{
    if (y1 >= y2 || num_barriers >= MAX_BARRIERS)
        return;

    barriers[num_barriers++] = XFixesCreatePointerBarrier(
        dpy,
        root,
        x,
        y1,
        x,
        y2,
        0,          /* aucune direction autorisée */
        0,
        NULL
    );

    printf("Barrière verticale : x=%d, y=%d..%d\n",
           x, y1, y2);
}

static void update_barriers(void)
{
    XRRScreenResources *res;

    int ix = 0;
    int iy = 0;
    int iw = 0;
    int ih = 0;

    RRCrtc internal_crtc = None;

    destroy_barriers();

    res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res)
        return;

    /*
     * Recherche de l'écran interne eDP.
     */
    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo *out =
            XRRGetOutputInfo(dpy, res, res->outputs[i]);

        if (!out)
            continue;

        if (out->connection == RR_Connected &&
            strncmp(out->name, "eDP", 3) == 0 &&
            out->crtc != None) {

            XRRCrtcInfo *crtc =
                XRRGetCrtcInfo(dpy, res, out->crtc);

            if (crtc && crtc->mode != None) {
                ix = crtc->x;
                iy = crtc->y;
                iw = (int)crtc->width;
                ih = (int)crtc->height;

                internal_crtc = out->crtc;

                XRRFreeCrtcInfo(crtc);
                XRRFreeOutputInfo(out);
                break;
            }

            if (crtc)
                XRRFreeCrtcInfo(crtc);
        }

        XRRFreeOutputInfo(out);
    }

    /*
     * Pas d'eDP actif :
     * Quand le G9 remplace l'écran interne.
     */
    if (internal_crtc == None) {
        printf("eDP non actif : aucune barrière\n");

        XRRFreeScreenResources(res);
        XFlush(dpy);
        return;
    }

    printf(
        "eDP : x=%d y=%d w=%d h=%d\n",
        ix, iy, iw, ih
    );

    /*
     * Parcourt tous les CRTCs actifs.
     */
    for (int i = 0; i < res->ncrtc; i++) {
        RRCrtc crtc_id = res->crtcs[i];

        if (crtc_id == internal_crtc)
            continue;

        XRRCrtcInfo *c =
            XRRGetCrtcInfo(dpy, res, crtc_id);

        if (!c)
            continue;

        if (c->mode == None ||
            c->width == 0 ||
            c->height == 0) {
            XRRFreeCrtcInfo(c);
            continue;
        }

        int x1 = c->x;
        int y1 = c->y;
        int x2 = c->x + (int)c->width;
        int y2 = c->y + (int)c->height;

        /*
         * ---------------------------------------------------------
         * Écran AU-DESSUS de eDP
         *
         * écran externe :
         *
         *      y2
         *      ↓
         * ───────────────────
         *      iy
         * ───────────────────
         *      eDP
         *
         * ---------------------------------------------------------
         */
        if (y2 == iy) {
            int left = x1 > ix ? x1 : ix;
            int right =
                x2 < ix + iw ? x2 : ix + iw;

            if (left < right) {
                add_horizontal_barrier(
                    left,
                    right,
                    iy
                );
            }
        }

        /*
         * ---------------------------------------------------------
         * Écran EN-DESSOUS de eDP
         * ---------------------------------------------------------
         */
        if (y1 == iy + ih) {
            int left = x1 > ix ? x1 : ix;
            int right =
                x2 < ix + iw ? x2 : ix + iw;

            if (left < right) {
                add_horizontal_barrier(
                    left,
                    right,
                    iy + ih
                );
            }
        }

        /*
         * ---------------------------------------------------------
         * Écran À GAUCHE de eDP
         * ---------------------------------------------------------
         */
        if (x2 == ix) {
            int top = y1 > iy ? y1 : iy;
            int bottom =
                y2 < iy + ih ? y2 : iy + ih;

            if (top < bottom) {
                add_vertical_barrier(
                    top,
                    bottom,
                    ix
                );
            }
        }

        /*
         * ---------------------------------------------------------
         * Écran À DROITE de eDP
         * ---------------------------------------------------------
         */
        if (x1 == ix + iw) {
            int top = y1 > iy ? y1 : iy;
            int bottom =
                y2 < iy + ih ? y2 : iy + ih;

            if (top < bottom) {
                add_vertical_barrier(
                    top,
                    bottom,
                    ix + iw
                );
            }
        }

        XRRFreeCrtcInfo(c);
    }

    XRRFreeScreenResources(res);

    printf(
        "%d barrière(s) active(s)\n",
        num_barriers
    );

    XFlush(dpy);
}

static void handle_signal(int sig)
{
    (void)sig;

    if (dpy) {
        destroy_barriers();
        XFlush(dpy);
        XCloseDisplay(dpy);
    }

    exit(EXIT_SUCCESS);
}

int main(void)
{
    dpy = XOpenDisplay(NULL);

    if (!dpy) {
        fprintf(
            stderr,
            "Impossible d'ouvrir DISPLAY\n"
        );
        return EXIT_FAILURE;
    }

    root = DefaultRootWindow(dpy);

    /*
     * Vérifie XFixes.
     */
    int fixes_event_base;
    int fixes_error_base;

    if (!XFixesQueryExtension(
            dpy,
            &fixes_event_base,
            &fixes_error_base)) {

        fprintf(
            stderr,
            "Extension XFixes indisponible\n"
        );

        XCloseDisplay(dpy);
        return EXIT_FAILURE;
    }

    /*
     * Vérifie RandR.
     */
    if (!XRRQueryExtension(
            dpy,
            &randr_event_base,
            &randr_error_base)) {

        fprintf(
            stderr,
            "Extension XRandR indisponible\n"
        );

        XCloseDisplay(dpy);
        return EXIT_FAILURE;
    }

    /*
     * Demande les événements RandR.
     */
    XRRSelectInput(
        dpy,
        root,
        RRScreenChangeNotifyMask |
        RRCrtcChangeNotifyMask |
        RROutputChangeNotifyMask
    );

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /*
     * Première configuration.
     */
    update_barriers();

    /*
     * Boucle principale.
     */
    for (;;) {
        XEvent event;

        XNextEvent(dpy, &event);

        if (event.type == randr_event_base +
                           RRScreenChangeNotify) {

            update_barriers();

        } else if (event.type == randr_event_base +
                                  RRNotify) {

            update_barriers();
        }
    }

    return EXIT_SUCCESS;
}
