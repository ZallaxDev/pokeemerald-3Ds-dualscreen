/*
 * Measured scenarios (include/3ds_platform.h). A measurement compares two
 * builds, or two settings, on the same frames: so a session is recorded once
 * from boot - the save it started from, the settings, every frame's input -
 * and played back as often as needed. sdmc:/3ds/emerald3ds/scenario.txt:
 *
 *   mode=record | play | queue   (anything else, or no file: off)
 *   name=route104          (sdmc:/3ds/emerald3ds/scenarios/route104/)
 *   slider=1.0             (optional: the 3D slider fixed at this value)
 *   exit=0                 (play: keep running when the input ends; default 1)
 *   voxel=1 ...            (play: any setting, for this session only)
 *   run=route104 slider=1.0 voxel_battle=1   (queue: one playback per launch)
 *
 * Recording copies the save (save.sav), writes input.bin and meta.txt, and
 * leaves the player's own save and settings as they always are. Playing loads
 * the recorded save and settings, never writes the card's save or settings,
 * feeds the recorded input frame by frame and fixes the game's clock to the
 * recorded start, a second every 60 frames: the game's random seed comes from
 * that clock. Both log game-state digests every 600 frames (3ds_game_full.c);
 * a playback compares them with the recording's, so a run that drifted from
 * it is named in the log instead of being measured as if it were the same.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "3ds_platform.h"

#define DIR "sdmc:/3ds/emerald3ds/"
#define CHECK_EVERY 600
#define CHECKS 512
#define INPUT_MAGIC 0x4e493345u   /* "E3IN" */

enum { OFF, RECORD, PLAY };

typedef struct
{
    uint16_t buttons, touch;
    int16_t circleX, circleY;
    uint16_t touchX, touchY;
} Sample;
_Static_assert(sizeof(Sample) == 12, "input.bin sample");

static int sMode = OFF;
static char sName[32], sPath[96];
static float sSlider = -1.0f;
static bool sExitAtEnd = true;
static int64_t sStart;
static uint32_t sFrame;
/* Recording: samples not yet on the card. Playing: the whole input. */
static Sample *sSamples;
static uint32_t sCount, sCapacity, sWritten;
static FILE *sInput;
static bool sDue, sEnded;
static uint32_t sExitFrame, sEscape;
static struct { uint32_t frame, digest; char what[12]; } sChecks[CHECKS];
static unsigned sCheckCount, sChecksOk, sChecksBad;

static void PathOf(char *path, size_t size, const char *file)
{
    snprintf(path, size, DIR "scenarios/%s/%s", sName, file);
}

static bool CopyFile(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out;
    static char buffer[16 * 1024];
    size_t n;
    bool ok = true;

    if (in == NULL)
        return false;
    out = fopen(to, "wb");
    if (out == NULL)
    {
        fclose(in);
        return false;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        ok = ok && fwrite(buffer, 1, n, out) == n;
    fclose(in);
    return fclose(out) == 0 && ok;
}

static void Trim(char *line)
{
    size_t n = strlen(line);

    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
        line[--n] = 0;
}

static void StartRecording(void)
{
    char path[128], settings[1024];
    FILE *meta;
    uint32_t header[4] = {INPUT_MAGIC, 1, sizeof(Sample), 0};

    mkdir(DIR "scenarios", 0777);
    snprintf(path, sizeof(path), DIR "scenarios/%s", sName);
    mkdir(path, 0777);
    PathOf(path, sizeof(path), "save.sav");
    remove(path);
    bool saved = CopyFile(DIR "emerald3ds.sav", path);
    PathOf(path, sizeof(path), "input.bin");
    sInput = fopen(path, "wb");
    if (sInput == NULL || fwrite(header, sizeof(header), 1, sInput) != 1)
    {
        CtrLog_Write(CTR_LOG_ERROR, "SCENARIO %s: cannot write %s", sName, path);
        sMode = OFF;
        return;
    }
    sCapacity = CHECK_EVERY;
    sSamples = malloc(sCapacity * sizeof(Sample));
    sStart = (int64_t)time(NULL);
    /* Fast-forward is a different run: recorded and played at normal speed. */
    CtrSettings_ApplyLine("speed=1");
    PathOf(path, sizeof(path), "meta.txt");
    meta = fopen(path, "w");
    if (meta != NULL)
    {
        CtrSettings_Text(settings, sizeof(settings));
        fprintf(meta, "start %lld\nsave %d\n%s", (long long)sStart, saved ? 1 : 0, settings);
        fclose(meta);
    }
    CtrLog_Write(CTR_LOG_BOOT, "SCENARIO record %s: save %s, clock %lld", sName,
                 saved ? "copied" : "none (new game)", (long long)sStart);
}

/* `overrides`: settings for this session, "key=value" lines. */
/*
 * A playback that cannot start says so on the screen and leaves: started as
 * the plain game instead, it looked like a measured run that had stopped
 * working. A queue has already moved past it, so the next launch goes on.
 */
static void NoRecording(const char *why)
{
    char detail[256];

    CtrLog_Write(CTR_LOG_ERROR, "SCENARIO %s: %s", sName, why);
    CtrLog_Close();
    snprintf(detail, sizeof(detail),
             "Scenario \"%s\": %s.\n\n"
             "Record it (scenario.txt: mode=record,\nname=%s) or remove it from the list.\n"
             "A queue skips it: the next launch\nplays the next run.",
             sName, why, sName);
    CtrPlatform_ShowDataError("Measured scenario not found", detail);
}

static void StartPlaying(const char *overrides)
{
    char path[128], line[96];
    FILE *meta, *input;
    uint32_t header[4];
    long frames = -1;

    PathOf(path, sizeof(path), "meta.txt");
    meta = fopen(path, "r");
    PathOf(path, sizeof(path), "input.bin");
    input = fopen(path, "rb");
    if (meta == NULL || input == NULL || fread(header, sizeof(header), 1, input) != 1
        || header[0] != INPUT_MAGIC || header[2] != sizeof(Sample))
    {
        if (meta) fclose(meta);
        if (input) fclose(input);
        NoRecording("no recording in scenarios/ (input.bin, meta.txt)");
    }
    /* The recorded settings over the defaults, then the scenario's own for
     * this session. */
    CtrSettings_Defaults();
    while (fgets(line, sizeof(line), meta))
    {
        long long start;
        unsigned frame, digest;
        char what[12];

        Trim(line);
        if (sscanf(line, "start %lld", &start) == 1)
            sStart = start;
        else if (sscanf(line, "frames %ld", &frames) == 1)
            ;
        else if (sscanf(line, "check %u %11s %x", &frame, what, &digest) == 3)
        {
            if (sCheckCount < CHECKS)
            {
                sChecks[sCheckCount].frame = frame;
                sChecks[sCheckCount].digest = digest;
                snprintf(sChecks[sCheckCount].what, sizeof(sChecks[0].what), "%s", what);
                ++sCheckCount;
            }
        }
        else if (strchr(line, '=') != NULL)
            CtrSettings_ApplyLine(line);
    }
    fclose(meta);
    for (const char *at = overrides; *at;)
    {
        size_t n = strcspn(at, "\n");

        snprintf(line, sizeof(line), "%.*s", (int)n, at);
        if (strchr(line, '=') != NULL)
            CtrSettings_ApplyLine(line);
        at += n + (at[n] != 0);
    }
    CtrSettings_ApplyLine("speed=1");
    CtrSettings_Freeze();
    fseek(input, 0, SEEK_END);
    sCount = (uint32_t)((ftell(input) - (long)sizeof(header)) / (long)sizeof(Sample));
    if (frames >= 0 && (uint32_t)frames < sCount)
        sCount = (uint32_t)frames;
    sSamples = malloc((sCount ? sCount : 1) * sizeof(Sample));
    fseek(input, sizeof(header), SEEK_SET);
    if (sSamples == NULL || fread(sSamples, sizeof(Sample), sCount, input) != sCount)
    {
        fclose(input);
        NoRecording("its input.bin cannot be read");
    }
    fclose(input);
    PathOf(sPath, sizeof(sPath), "save.sav");
    CtrLog_Write(CTR_LOG_BOOT, "SCENARIO play %s: %lu frames, %u checks, clock %lld, slider %s",
                 sName, (unsigned long)sCount, sCheckCount, (long long)sStart,
                 sSlider >= 0.0f ? "fixed" : "physical");
}

/* One "key=value": the scenario's own keys here, any other is a setting
 * added to `overrides`. */
static void Option(const char *option, char *overrides, size_t size)
{
    float slider;
    int exitAtEnd;

    if (sscanf(option, "name=%31[A-Za-z0-9_-]", sName) == 1)
        return;
    if (sscanf(option, "slider=%f", &slider) == 1)
        sSlider = slider < 0.0f ? 0.0f : slider > 1.0f ? 1.0f : slider;
    else if (sscanf(option, "exit=%d", &exitAtEnd) == 1)
        sExitAtEnd = exitAtEnd != 0;
    else if (strchr(option, '=') != NULL && strncmp(option, "mode=", 5) && strncmp(option, "run=", 4))
    {
        size_t used = strlen(overrides);

        snprintf(overrides + used, size - used, "%s\n", option);
    }
}

/*
 * mode=queue: a list of playbacks, one per launch of the game, so that a
 * whole measurement is "start the game, leave it, start it again" without
 * editing the card between runs. Each "run=NAME key=value ..." line is a
 * playback; scenarios/queue.txt counts the ones already started. A run's
 * port.log and perf.bin are kept, at the next launch, as
 * scenarios/results/NN_NAME_<options>.log/.bin.
 */
#define RUNS 32

static bool NextRun(FILE *control, char *overrides, size_t size, char *label, size_t labelSize)
{
    char line[160], runs[RUNS][120], pending[96] = "";
    unsigned count = 0, next = 0;
    FILE *queue = fopen(DIR "scenarios/queue.txt", "r");

    if (queue != NULL)
    {
        while (fgets(line, sizeof(line), queue))
        {
            Trim(line);
            if (sscanf(line, "next %u", &next) != 1)
                sscanf(line, "pending %95s", pending);
        }
        fclose(queue);
    }
    /* The last run's files, rotated to *-prev by this launch's log. */
    if (pending[0])
    {
        char to[160];

        mkdir(DIR "scenarios/results", 0777);
        snprintf(to, sizeof(to), DIR "scenarios/results/%s.log", pending);
        remove(to);
        rename(DIR "port-prev.log", to);
        snprintf(to, sizeof(to), DIR "scenarios/results/%s.bin", pending);
        remove(to);
        rename(DIR "perf-prev.bin", to);
    }
    rewind(control);
    while (fgets(line, sizeof(line), control))
    {
        Trim(line);
        if (strncmp(line, "run=", 4) == 0 && count < RUNS)
            snprintf(runs[count++], sizeof(runs[0]), "%.119s", line + 4);
    }
    queue = fopen(DIR "scenarios/queue.txt", "w");
    if (next >= count)
    {
        if (queue) { fprintf(queue, "next %u\n", next); fclose(queue); }
        CtrLog_Write(CTR_LOG_BOOT, "SCENARIO queue complete: %u runs (scenarios/results); "
                     "delete scenarios/queue.txt to run it again", count);
        return false;
    }
    /* "NAME key=value ...": the name, then options. */
    char *run = runs[next], *token = strtok(run, " ");
    size_t used;

    snprintf(sName, sizeof(sName), "%s", token ? token : "default");
    used = (size_t)snprintf(label, labelSize, "%02u_%s", next + 1, sName);
    while ((token = strtok(NULL, " ")) != NULL)
    {
        Option(token, overrides, size);
        /* The label names the options too: "slider=1.0" becomes "slider1.0". */
        if (used < labelSize - 2)
        {
            label[used++] = '_';
            for (const char *c = token; *c && used < labelSize - 1; ++c)
                if (*c != '=') label[used++] = *c;
            label[used] = 0;
        }
    }
    if (queue)
    {
        fprintf(queue, "next %u\npending %s\n", next + 1, label);
        fclose(queue);
    }
    CtrLog_Write(CTR_LOG_BOOT, "SCENARIO queue run %u of %u: %s", next + 1, count, label);
    return true;
}

void CtrScenario_Init(void)
{
    FILE *control = fopen(DIR "scenario.txt", "r");
    static char overrides[512];
    char line[160], mode[16] = "", label[96];

    if (control == NULL)
        return;
    while (fgets(line, sizeof(line), control))
    {
        Trim(line);
        if (sscanf(line, "mode=%15s", mode) != 1)
            Option(line, overrides, sizeof(overrides));
    }
    if (sName[0] == 0)
        strcpy(sName, "default");
    if (strcmp(mode, "record") == 0)
    {
        sMode = RECORD;
        StartRecording();
    }
    else if (strcmp(mode, "play") == 0)
    {
        sMode = PLAY;
        StartPlaying(overrides);
    }
    else if (strcmp(mode, "queue") == 0)
    {
        /* Only the run's own options: none of the file's. */
        overrides[0] = 0;
        sSlider = -1.0f;
        if (NextRun(control, overrides, sizeof(overrides), label, sizeof(label)))
        {
            sMode = PLAY;
            StartPlaying(overrides);
        }
    }
    fclose(control);
}

bool CtrScenario_Active(void) { return sMode != OFF; }
bool CtrScenario_Playing(void) { return sMode == PLAY; }

static void Flush(void)
{
    if (sInput == NULL || sWritten == sCount)
        return;
    fwrite(sSamples, sizeof(Sample), sCount - sWritten, sInput);
    fflush(sInput);
    sWritten = sCount;
}

void CtrScenario_Input(CtrInputSample *sample)
{
    if (sMode == OFF)
        return;
    ++sFrame;
    if (sFrame % CHECK_EVERY == 0)
        sDue = true;
    if (sMode == RECORD)
    {
        if (sSamples == NULL)
            return;
        if (sCount - sWritten == sCapacity)
        {
            /* On the card every 600 frames: one short write, not one a frame. */
            fwrite(sSamples, sizeof(Sample), sCapacity, sInput);
            fflush(sInput);
            sWritten = sCount;
        }
        sSamples[sCount - sWritten] = (Sample){sample->buttons, sample->touchActive,
            sample->circleX, sample->circleY, sample->touchX, sample->touchY};
        ++sCount;
        return;
    }
    /* Playing. START+SELECT held for a second still leaves the game. */
    sEscape = (sample->buttons & (CTR_KEY_START | CTR_KEY_SELECT)) == (CTR_KEY_START | CTR_KEY_SELECT)
              ? sEscape + 1 : 0;
    if (sEscape >= 60)
        CtrPlatform_RequestExit();
    if (sFrame <= sCount)
    {
        const Sample *s = &sSamples[sFrame - 1];

        sample->buttons = s->buttons;
        sample->touchActive = s->touch != 0;
        sample->circleX = s->circleX;
        sample->circleY = s->circleY;
        sample->touchX = s->touchX;
        sample->touchY = s->touchY;
        return;
    }
    memset(sample, 0, sizeof(*sample));
    if (!sEnded)
    {
        sEnded = true;
        sDue = true;
        sExitFrame = sFrame + 60;
        CtrLog_Write(CTR_LOG_BOOT, "SCENARIO end of input at frame %lu: checks ok=%u diverged=%u of %u recorded",
                     (unsigned long)sFrame, sChecksOk, sChecksBad, sCheckCount);
    }
    else if (sExitAtEnd && sFrame == sExitFrame)
        CtrPlatform_RequestExit();
}

bool CtrScenario_Clock(int64_t *seconds)
{
    if (sMode == OFF)
        return false;
    *seconds = sStart + sFrame / 60;
    return true;
}

const char *CtrScenario_SavePath(void)
{
    return sMode == PLAY ? sPath : NULL;
}

bool CtrScenario_SaveWrites(void)
{
    return sMode != PLAY;
}

static float sForcedSlider = -1.0f;

void CtrScenario_ForceSlider(float value)
{
    sForcedSlider = value;
}

float CtrScenario_Slider(float physical)
{
    if (sForcedSlider >= 0.0f)
        return sForcedSlider;
    return sMode != OFF && sSlider >= 0.0f ? sSlider : physical;
}

bool CtrScenario_CheckpointDue(void)
{
    bool due = sDue;

    sDue = false;
    return due;
}

void CtrScenario_Checkpoint(const char *what, uint32_t digest)
{
    if (sMode == RECORD)
    {
        /* Kept until the end (CtrScenario_Close): no card write mid-game. */
        if (sCheckCount < CHECKS)
        {
            sChecks[sCheckCount].frame = sFrame;
            sChecks[sCheckCount].digest = digest;
            snprintf(sChecks[sCheckCount].what, sizeof(sChecks[0].what), "%s", what);
            ++sCheckCount;
        }
        CtrLog_Write(CTR_LOG_GAME, "SCENARIO check f=%lu %s %08lx", (unsigned long)sFrame, what,
                     (unsigned long)digest);
    }
    else if (sMode == PLAY)
    {
        for (unsigned i = 0; i < sCheckCount; ++i)
            if (sChecks[i].frame == sFrame && strcmp(sChecks[i].what, what) == 0)
            {
                bool same = sChecks[i].digest == digest;

                same ? ++sChecksOk : ++sChecksBad;
                CtrLog_Write(same ? CTR_LOG_GAME : CTR_LOG_ERROR, "SCENARIO check f=%lu %s %08lx %s",
                             (unsigned long)sFrame, what, (unsigned long)digest,
                             same ? "same as recorded" : "DIVERGED from the recording");
                return;
            }
        CtrLog_Write(CTR_LOG_GAME, "SCENARIO check f=%lu %s %08lx (not in the recording)",
                     (unsigned long)sFrame, what, (unsigned long)digest);
    }
}

/* At exit. A recording ends with the START+SELECT that left the game: those
 * frames are not part of it, or every playback would leave there too. */
void CtrScenario_Close(void)
{
    if (sMode == RECORD && sInput != NULL)
    {
        char path[128];
        FILE *meta;
        uint32_t frames;

        Flush();
        fclose(sInput);
        sInput = NULL;
        frames = sCount;
        /* The tail is on the card; count back over it there. */
        PathOf(path, sizeof(path), "input.bin");
        FILE *input = fopen(path, "rb");
        if (input != NULL)
        {
            Sample s;

            while (frames > 0 && fseek(input, 16 + (long)(frames - 1) * (long)sizeof(Sample), SEEK_SET) == 0
                   && fread(&s, sizeof(s), 1, input) == 1
                   && s.buttons != 0 && (s.buttons & ~(CTR_KEY_START | CTR_KEY_SELECT)) == 0)
                --frames;
            fclose(input);
        }
        PathOf(path, sizeof(path), "meta.txt");
        meta = fopen(path, "a");
        if (meta != NULL)
        {
            for (unsigned i = 0; i < sCheckCount; ++i)
                if (sChecks[i].frame <= frames)
                    fprintf(meta, "check %lu %s %08lx\n", (unsigned long)sChecks[i].frame, sChecks[i].what,
                            (unsigned long)sChecks[i].digest);
            fprintf(meta, "frames %lu\n", (unsigned long)frames);
            fclose(meta);
        }
        CtrLog_Write(CTR_LOG_BOOT, "SCENARIO recorded %s: %lu frames", sName, (unsigned long)frames);
    }
    else if (sMode == PLAY)
        CtrLog_Write(CTR_LOG_BOOT, "SCENARIO played %s to frame %lu: checks ok=%u diverged=%u of %u recorded",
                     sName, (unsigned long)sFrame, sChecksOk, sChecksBad, sCheckCount);
    sMode = OFF;
}
