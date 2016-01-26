/*
** Copyright 2014 Guillaume Filion, Eduard Valera Zorita and Pol Cusco.
**
** File authors:
**  Pol Cuscó Pons    (polcusco@gmail.com)
**  Guillaume Filion  (guillaume.filion@gmail.com)
**
** License:
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
*/


#include <stdio.h>
#include <getopt.h>
#include "debug.h"
#include "parse.h"
#include "predict.h"
#include "zerone.h"


const char *USAGE =
"\n"
"USAGE:"
"  zerone [options] <input file 1> ... <input file n>\n"
"\n"
"  Input options\n"
"    -0 --mock: given file is a mock control\n"
"    -1 --chip: given file is a ChIP-seq experiment\n"
"\n"
"  Output options\n"
"    -l --list-output: output list of targets (default table)\n"
"    -c --confidence: also output confidence score (not available with -l)\n"
"\n"
"  Other options\n"
"    -h --help: display this message and exit\n"
"       --version: display version and exit\n"
"\n"
"EXAMPLES:\n"
" zerone --mock file1.bam,file2.bam --chip file3.bam,file4.bam\n"
" zerone -l -0 file1.map -1 file2.map -1 file4.map\n";


#define VERSION "zerone-v1.0"
#define MAXNARGS 255



//  -----------  Definitions of local one-liners  ----------- //
void say_usage(void) { fprintf(stderr, "%s\n", USAGE); }
void say_version(void) { fprintf(stderr, VERSION "\n"); }


void
parse_fname
(
   char ** names,
   char *  value,
   int  *  offset
)
// Several file names may be passed as comma-separated values.
{


   char *token;
   while ((token = strsep(&value, ",")) != NULL) {
      if (*offset >= MAXNARGS) {
         fprintf(stderr, "too many arguments\n");
         exit(EXIT_FAILURE);
      }
      names[*offset] = strndup(token, 256);
      if (names[*offset] == NULL) {
         fprintf(stderr, "memory error\n");
         exit(EXIT_FAILURE);
      }
      (*offset)++;
   }

   return;

}


int main(int argc, char **argv) {

   debug_print("%s (DEBUG)\n", VERSION);

   if (argc == 1) {
      say_usage();
      exit(EXIT_SUCCESS);
   }

   // Input file names (mock and ChIP).
   char *mock_fnames[MAXNARGS+1] = {0};
   char *ChIP_fnames[MAXNARGS+1] = {0};

   int n_mock_files = 0;
   int n_ChIP_files = 0;

   static int list_flag = 0;
   static int conf_flag = 0;

   // Parse options.
   while(1) {
      int option_index = 0;
      static struct option long_options[] = {
         {"list-output", no_argument,       &list_flag, 1 },
         {"confidence",  no_argument,       &conf_flag, 1 },
         {"mock",        required_argument,         0, '0'},
         {"chip",        required_argument,         0, '1'},
         {"help",        no_argument,               0, 'h'},
         {"version",     no_argument,               0, 'v'},
         {0, 0, 0, 0}
      };

      int c = getopt_long(argc, argv, "0:1:chl",
            long_options, &option_index);

      // Done parsing named options. //
      if (c == -1) break;

      switch (c) {
      case 0:
         break;

      case '0':
         parse_fname(mock_fnames, optarg, &n_mock_files);
         break;

      case '1':
         parse_fname(ChIP_fnames, optarg, &n_ChIP_files);
         break;

      case 'h':
         say_usage();
         return EXIT_SUCCESS;

      case 'l':
         list_flag = 1;
         break;

      case 'c':
         conf_flag = 1;
         break;

      case 'v':
         say_version();
         return EXIT_SUCCESS;

      default:
         // Cannot parse. //
         say_usage();
         return EXIT_FAILURE;

      }

   }

   // Now parse positional arguments (file names).
   while (optind < argc) {
      parse_fname(ChIP_fnames, argv[optind++], &n_ChIP_files);
   }

   debug_print("%s", "done parsing arguments\n");

   // Check options.
   if (conf_flag && list_flag) {
      fprintf(stderr, "ignoring --confidence flag for list output\n");
      conf_flag = 0;
   }

   // Process input files.
   ChIP_t *ChIP = parse_input_files(mock_fnames, ChIP_fnames);

   if (ChIP == NULL) {
      fprintf(stderr, "error while reading input\n");
      exit(EXIT_FAILURE);
   }

   debug_print("%s", "done reading input files\n");
   debug_print("ChIP: r = %ld\n", ChIP->r);
   debug_print("ChIP: nb = %d\n", ChIP->nb);
   for (int i = 0 ; i < ChIP->nb ; i++) {
      debug_print("ChIP: block %s (%d)\n",
            ChIP->nm + 32*i, ChIP->sz[i]);
   }

   // Do zerone.
   zerone_t *Z = do_zerone(ChIP);

   if (Z == NULL) {
      fprintf(stderr, "run time error (sorry)\n");
      exit(EXIT_FAILURE);
   }

   debug_print("map: %d, %d, %d\n", Z->map[0], Z->map[1], Z->map[2]);
   debug_print("%s", "Q:\n");
   debug_print("%.3f %.3f %.3f\n", Z->Q[0], Z->Q[3], Z->Q[6]);
   debug_print("%.3f %.3f %.3f\n", Z->Q[1], Z->Q[4], Z->Q[7]);
   debug_print("%.3f %.3f %.3f\n", Z->Q[2], Z->Q[5], Z->Q[8]);

   debug_print("%s", "p:\n");
   for (int j = 0 ; j < 3 ; j++) {
      int off = 0;
      char debuf[512];
      for (int i = 0 ; i < Z->r+1 ; i++) {
         off += sprintf(debuf + off, "%.3f ", Z->p[i+j*(Z->r+1)]);
         if (off > 499) break;
      }
      debug_print("%s\n", debuf);
   }

   // Quality control.
   double QCscore = zerone_predict(Z);
   fprintf(stdout, "# QC score: %.3f\n", QCscore);
   fprintf(stdout, "# advice: %s discretization.\n",
         QCscore >= 0 ? "accept" : "reject");

   if (list_flag) {
      int wid = 0;
      int target = 0;
      for (int i = 0 ; i < ChIP->nb ; i++) {
         char *name = ChIP->nm + 32*i;
         for (int j = 0 ; j < ChIP->sz[i] ; j++) {
            if (!target && Z->path[wid] == Z->map[2]) {
               fprintf(stdout, "%s\t%d\t", name, 300*j + 1);
               target = 1;
            }
            else if (target && Z->path[wid] != Z->map[2]) {
               fprintf(stdout, "%d\n", 300*(j+1));
               target = 0;
            }
            wid++;
         }
         if (target) {
            fprintf(stdout, "%d\n", 300 * ChIP->sz[i]);
            target = 0;
         }
      }
   }

   else {
      int wid = 0;
      for (int i = 0 ; i < ChIP->nb ; i++) {
         char *name = ChIP->nm + 32*i;
         for (int j = 0 ; j < ChIP->sz[i] ; j++) {
            fprintf(stdout, "%s\t%d\t%d\t%d", name, 300*j + 1,
                  300*(j+1), Z->path[wid]);
            for (int k = 0 ; k < Z->ChIP->r ; k++) {
               fprintf(stdout, "\t%d", Z->ChIP->y[k+wid*Z->ChIP->r]);
            }
            // Print confidence if required.
            if (conf_flag) {
               fprintf(stdout, "\t%.3f", Z->phi[2+wid*3]);
            }
            fprintf(stdout, "\n");
            wid++;
         }
      }
   }

   destroy_zerone_all(Z); // Also frees ChIP.

   for (int i = 0 ; i < MAXNARGS ; i++) {
      free(mock_fnames[i]);
      free(ChIP_fnames[i]);
   }

   return 0;

}
