// cscribe.c - Chris Laverdiere 2015

#define _GNU_SOURCE
#define SAMPLE_RATE 44100

#include <libgen.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include <ncurses.h>
#include <portaudio.h>
#include <sndfile.h>

#define TEMPO_DELTA .05
#define TIME_DELTA 2

struct pa_data {
  SNDFILE *sndfile;
  SF_INFO sf_info;
  int pos;
} audio_data;

struct progress_bar {
  int row, col;
  int len;
  float progress;
} pbar;

struct song {
  int len;
  int time;
  int mark;
  float tempo;
  char* name;
} current_song;


void cleanup();

void* init_audio();
void init_curses();

void seek_seconds(int);
void set_mark(int);
void set_tempo(float);

void show_help();
void show_greeting();
void* show_main();
void show_modeline();
void show_progress_bar();
void show_song_info();

static int max_col, max_row;
static int quit, pa_on, curses_on;
static int redraw_flag;
static char* mode_line;
static PaError err;
static PaStream *stream;
static char* welcome_msg = "Welcome to cscribe!\n\n";

static int pa_callback(const void *input_buf,
                       void *output_buf,
                       unsigned long frame_cnt,
                       const PaStreamCallbackTimeInfo* time_info,
                       PaStreamCallbackFlags flags,
                       void *data) {

  struct pa_data* cb_data = (struct pa_data*) data;
  int* out = (int *) output_buf;
  int* cursor = out;
  int size = frame_cnt;
  int read;
  int frames_left;

  while (size > 0) {
    sf_seek(cb_data->sndfile, cb_data->pos, SEEK_SET);
    frames_left = cb_data->sf_info.frames - cb_data->pos;

    if (size > frames_left) {
      read = frames_left;
      cb_data->pos = 0;
    } else {
      read = size;
      cb_data->pos += read;
    }

    sf_readf_int(cb_data->sndfile, cursor, read);
    cursor += read;
    size -= read;
  }

  return paContinue;
}

void pa_error(PaError err) {
  if (err != paNoError) {
    fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
    exit(1);
  }
}

// Prints text centered horizontally.
void printw_center_x(int row, int max_col, char* fmt, ...) {
  char* str = NULL;
  va_list args;

  va_start(args, fmt);

  vasprintf(&str, fmt, args);
  mvprintw(row, (max_col / 2) - (strlen(str) / 2), str);

  va_end(args);

  free(str);
}

// TODO use a callback?
void seek_seconds(int n) {
  current_song.time = MIN(MAX(n, 0), current_song.len);
  pbar.progress = (float) current_song.time / current_song.len;
  show_progress_bar();
  show_song_info();
}

void set_mark(int n) {
  current_song.mark = n;
  show_song_info();
  show_progress_bar();
}

void set_tempo(float f) {
  current_song.tempo = MAX(0, f);
  show_song_info();
}

// Prints a help menu with all commands.
void show_help() {
  clear();

  printw_center_x(1, max_col, "cscribe help:\n\n");

  printw("': Jump to mark\n");
  printw("<: Decrease tempo\n");
  printw(">: Increase tempo\n");
  printw("h: Show / exit this help menu\n");
  printw("j: Back 2 seconds\n");
  printw("k: Forward 2 seconds\n");
  printw("m: Create mark\n");
  printw("o: Open file\n");
  printw("q: Quit cscribe\n");

  refresh();

  getch();

  show_main();
}

void show_greeting() {
  printw_center_x(1, max_col, welcome_msg);

  if (current_song.name == NULL) {
    printw("Type ");
    addch('o' | A_BOLD);
    printw(" to open an audio file.\n");
  } else {
    show_song_info();
  }

}

void show_modeline() {
  mvprintw(max_row - 1, 0, mode_line);
}

// Displays the main screen.
void* show_main(void* args) {
  int ch;

  if (!curses_on) {
    init_curses();
    usleep(50000);
  }

  // TODO We should use a separate window for the modeline.
  if (mode_line == NULL) {
    mode_line = "Type h for the list of all commands.";
  }

  clear();
  show_greeting();
  show_modeline();
  show_progress_bar();

  while (!quit && (ch = getch())) {
    if (redraw_flag) {
      redraw_flag = 0;
      show_main(NULL);
    }

    switch(ch) {
      case '\'':
        seek_seconds(current_song.mark);
        break;
      case '<':
        set_tempo(current_song.tempo - TEMPO_DELTA);
        break;
      case '>':
        set_tempo(current_song.tempo + TEMPO_DELTA);
        break;
      case 'q':
        quit = 1;
        return NULL;
      case 'j':
        seek_seconds(current_song.time - TIME_DELTA);
        break;
      case 'k':
        seek_seconds(current_song.time + TIME_DELTA);
        break;
      case 'm':
        set_mark(current_song.time);
        break;
      case 'h':
        show_help();
        break;
    }
  }

  return NULL;
}

// Places a progress bar starting at row and col.
// progress is between 0 and 1.
void show_progress_bar() {
  int pos = 0;
  int mark_pos = (float) current_song.mark / current_song.len * pbar.len;

  pbar.col = max_col / 4;
  pbar.row = max_row / 2;
  pbar.len = max_col / 2;

  mvaddch(pbar.row, pbar.col, '[');

  while ((float) pos++ / pbar.len < pbar.progress) {
    mvaddch(pbar.row, pbar.col+pos, '=');
  }

  while (pos++ < pbar.len) {
    mvaddch(pbar.row, pbar.col+pos, ' ');
  }

  if (current_song.mark != 0) {
    mvaddch(pbar.row, pbar.col + mark_pos + 1, '*');
  }

  mvaddch(pbar.row, pbar.col+pos-1, ']');

  refresh();
}

void show_song_info() {
  printw_center_x(max_row / 2 - 2, max_col, basename(current_song.name));
  printw_center_x(max_row / 2 + 2, max_col, "%d:%02d | x%.2f",
                  current_song.time / 60, current_song.time % 60, current_song.tempo);

  if (current_song.mark != 0) {
    printw_center_x(max_row / 2 + 6, max_col, "(*) mark set at %d:%02d",
                    current_song.mark / 60, current_song.mark % 60);
  }
}

void* init_audio(void* args) {
  PaStreamParameters out_params;

  audio_data.pos = 0;
  audio_data.sf_info.format = 0;
  audio_data.sndfile = sf_open(current_song.name, SFM_READ, &audio_data.sf_info);

  if (!audio_data.sndfile) {
    fprintf(stderr, "Couldn't open %s\n", current_song.name);
  }

  Pa_Initialize();
  pa_on = 1;

  out_params.device = Pa_GetDefaultOutputDevice();
  out_params.channelCount = audio_data.sf_info.channels;
  out_params.suggestedLatency = 0.2;
  out_params.sampleFormat = paInt32;
  out_params.hostApiSpecificStreamInfo = 0;

  Pa_OpenStream(&stream, 0, &out_params, audio_data.sf_info.samplerate,
                      paFramesPerBufferUnspecified, paNoFlag, pa_callback,
                      &audio_data);

  Pa_StartStream(stream);

  while (Pa_IsStreamActive(stream) == 1 && !quit) {
    Pa_Sleep(100);
  }

  Pa_StopStream(stream);

  return NULL;
}

void init_curses() {
  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  getmaxyx(stdscr, max_row, max_col);

  curses_on = 1;
}

void cleanup() {
  if (pa_on) Pa_Terminate();
  if (curses_on) endwin();
}

int main(int argc, char* argv[])
{
  pthread_t curses_thread, audio_thread;

  atexit(cleanup);

  if (argc > 2) {
    fprintf(stderr, "cscribe <audio_file>\n");
  }

  if (argc == 2) {
    char* audio_name = strdup(argv[1]);

    current_song.name = audio_name;
    current_song.len = 200; // TODO placeholder
    current_song.tempo = 1.0;
  }

  pthread_create(&audio_thread, NULL, init_audio, NULL);
  pthread_create(&curses_thread, NULL, show_main, NULL);

  pthread_join(curses_thread, NULL);
  pthread_join(audio_thread, NULL);

  return 0;
}
