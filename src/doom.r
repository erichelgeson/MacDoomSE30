/* doom.r — Resource definitions for Doom SE/30 */

#include "Retro68APPL.r"

/* SIZE resource: request 4MB preferred, 3MB minimum */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    needsActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    4 * 1024 * 1024,     /* preferred:  4 MB */
    3 * 1024 * 1024      /* minimum:   3 MB */
};
