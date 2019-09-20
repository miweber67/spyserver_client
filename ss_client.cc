#include <iostream>
#include <fstream>

#include <getopt.h>

#include "tcp_client.h"
#include "ss_client_if.h"

typedef struct settings {
   double center_freq;
   double sample_rate;
   double gain;
   double dig_gain;
   uint32_t fft_bins;
   char* server;
   int port;
   unsigned long samples;
   int fft_average_seconds;
   char* samples_outfilename;
   char* fft_outfilename;
   uint8_t do_iq;
   uint8_t do_fft;
   uint8_t oneshot;
   uint8_t sample_bits;
   
} SettingsT;


void usage(char* appname) {
   
   static bool printed = false;
   
   if(!printed) {
      std::cout << "Usage: " << appname << " [-options] <mode> [iq_outfile] [fft_outfile]\n"
                << "\n  mode: one of  iq | fft | both"
                << "\n  -f <center frequency>"
                << "\n  -s <sample_rate>"
                << "\n  [-b <bits>, '8' or '16', default 16; 8 is EXPERIMENTAL]"
                << "\n  [-d <digital gain> - experimental, 0.0 .. 1.0]"
                << "\n  [-g <gain>]"
                << "\n  [-i  <integration interval for fft data> (default: 10 seconds)]"
                << "\n  [-r <server>]"
                << "\n  [-p <port>]"
                << "\n  [-n <num_samples>]"
                << "\n  [<iq outfile name>] ( '-' for stdout; optional, but must be specified if an fft outfilename is also provided)"
                << "\n  [<fft outfile name>] default log_power.csv"
                << std::endl;
      printed = true;
   }
}

void parse_args(int argc, char* argv[], SettingsT& settings)
{

   settings.center_freq = 403000000;
   settings.sample_rate = 10000000;
   settings.gain = 20;
   settings.dig_gain = 0;
   settings.server = strdup("127.0.0.1");
   settings.port = 5555;
   settings.samples = 0;
   settings.fft_average_seconds = 10;
   settings.fft_bins = 32767;
   settings.do_iq = 0;
   settings.do_fft = 0;
   settings.samples_outfilename = strdup("-");
   settings.fft_outfilename = strdup("log_power.csv");
   settings.oneshot = 0;
   settings.sample_bits = 16;
   
   int opt;
   
	while ((opt = getopt(argc, argv, "b:d:f:g:i:n:p:r:s:h1")) != -1) {
		switch (opt) {
		case 'b': // sample_bits
         settings.sample_bits = atoi(optarg);
         if( settings.sample_bits != 8 && settings.sample_bits != 16 ) {
            std::cerr << "sample bits value " << optarg << " must be 8 or 16\n";
            usage(argv[0]);
            exit(0);
         }
         break;
      case '1': // digital gain
         settings.oneshot = 1;
         break;
      case 'd': // digital gain
         settings.dig_gain = strtod(optarg, NULL);
         break;
      case 'f': // frequency
         settings.center_freq = strtod(optarg, NULL);
         break;
      case 's': // sampling rate
         settings.sample_rate = strtod(optarg, NULL);
         break;
      case 'g': // gain
	      settings.gain = strtod(optarg, NULL);
	      break;
      case 'r': // seRveR
	      settings.server = strdup(optarg);
	      break;
      case 'n': // # samples
	      settings.samples = strtol(optarg, NULL, 0);
	      break;
      case 'p': // port
	      settings.port = atoi(optarg);
	      break;
      case 'i': // integration interval
	      settings.fft_average_seconds = atoi(optarg);
	      break;
      case 't': // do FFT
	      settings.do_fft = 1;
	      break;
      case 'h': // help
	      usage(argv[0]);
	      exit(0);
	      break;
      default:
	      usage(argv[0]);
	      break;
      }
	} // end arg flags

   bool got_mode_string = false;	
   if(optind < argc) {
      if( 0 == strcmp("iq", argv[optind]) ) {
	      settings.do_iq = 1;
	      got_mode_string = true;
	   } else if( 0 == strcmp("fft", argv[optind]) ) {
	      settings.do_fft = 1;
	      got_mode_string = true;
	   } else if( 0 == strcmp("both", argv[optind]) ) {
	      settings.do_iq = 1;
	      settings.do_fft = 1;
	      got_mode_string = true;
	   } else {
	      std::cerr << "Unrecognized mode string '" << argv[optind] << "'\n";
		   usage(argv[0]);
		   exit(0);
	   }
	   
	   ++optind;
   } 
   
   if( !got_mode_string )
   {
      std::cerr << "Mode string required!" << std::endl;
	   usage(argv[0]);
	   exit(0);
   }   

	if(optind < argc) {
	   settings.samples_outfilename = argv[optind];
	   ++optind;
	}

	if(optind < argc) {
	   settings.fft_outfilename = argv[optind];
	   ++optind;
	}
	
   if( 0 == strcmp(settings.samples_outfilename, settings.fft_outfilename) ) {
      std::cerr << "Refusing to emit both samples and fft data to the same output stream! :-p\n";
      usage(argv[0]);
      exit(1);      
   }	
}


double get_monotonic_seconds() {

   double result = 0;
   struct timespec ts;
   if( 0 == clock_gettime(CLOCK_MONOTONIC, &ts) ) {
      result = ts.tv_sec + (ts.tv_nsec / double(1e9));
   } else {
      std::cerr << "Failed to get CLOCK_MONOTONIC!\n";
   }
   
   return result;
}

void fft_work_thread( ss_client_if& server,
                      const SettingsT& settings,
                      bool& running ) {

   std::vector<uint32_t> fft_data;
   int periods = 0;
   std::vector<uint32_t> fft_data_sums;
   int sum_periods = 0;
   double hz_low = settings.center_freq - (settings.sample_rate / 2.0);
   double hz_high = settings.center_freq + (settings.sample_rate / 2.0);

   std::cerr << "fft_work_thread: center_freq: " << settings.center_freq
             << "                 sample_rate: " << settings.sample_rate
             << "                    fft_bins: " << settings.fft_bins
             << "                      hz_low: " << hz_low
             << "                     hz_high: " << hz_high
             << std::endl;
             
   double last_start = get_monotonic_seconds();

//   std::cerr << "fft_work_thread starting\n";
   
   while( running ) {
   
      server.get_fft_data( fft_data, periods );
      
//      std::cerr << "fft_work_thread got some data with " << periods << " periods\n";

      double hz_step = settings.sample_rate / fft_data.size();
      // TODO: Configure fft bins in source interface and these sizes up front
      if( fft_data_sums.size() < fft_data.size() ) {
         fft_data_sums.resize(fft_data.size());
      }
      
      if( fft_data.size() > 0 && periods > 0 ) {
         // accumulate data
         size_t num_pts = fft_data.size();
         for (size_t i = 0; i < num_pts; ++i)
         {
            fft_data_sums[i] += fft_data[i];
         }
         sum_periods += periods;
      }

      double now = get_monotonic_seconds();
      
      if( now - last_start > settings.fft_average_seconds ) {
         // dump to output file
         // create rtl_power-like header
         // # date, time, Hz low, Hz high, Hz step, samples, dB, dB, dB, ...
         // need only hz low and hz step
         std::ofstream outfile (settings.fft_outfilename);
         outfile << "date, time, " << hz_low << ", "
                 << hz_high << ", "
                 << hz_step << ", "
                 << "samples" << ", ";
                 
         size_t num_pts = fft_data_sums.size();
         for (size_t i = 0; i < num_pts; ++i)
         {
            outfile << (fft_data_sums[i] / sum_periods);
            if( i < num_pts - 1 ) {
               outfile << ", ";
            }
            fft_data_sums[i] = 0;
         }
         outfile << std::endl;

         sum_periods = 0;
         last_start = now;
         
         if( settings.oneshot == 1 ) {
            outfile.close();
            running = false;         
         }

//         std::cerr << "log file updated" << std::endl;
      
      }
      
   }
//   std::cerr << "fft_work_thread ending\n";

}

int main(int argc, char* argv[]) {

   const unsigned int batch_sz = 32768;
   unsigned int rxd = 0;
   SettingsT settings;
   
   parse_args(argc, argv, settings);
      
   ss_client_if server (settings.server, settings.port, settings.do_iq, settings.do_fft, settings.fft_bins, settings.sample_bits);

   std::cerr << "ss_client: setting center_freq to " << settings.center_freq << std::endl;
   if(!server.set_center_freq(settings.center_freq)) {
      std::cerr << "Failed to set freq\n";
      exit(1);
   }

   if(!server.set_gain(settings.gain)) {
      std::cerr << "Failed to set gain\n";
      exit(1);
   }

   if(!server.set_gain(settings.dig_gain, "Digital")) {
      std::cerr << "Failed to set digital gain\n";
      exit(1);
   }
 
   if(!server.set_sample_rate(settings.sample_rate)) {
      std::cerr << "Failed to set sample rate\n";
      exit(1);
   }

   server.start();

   std::thread* fft_thread (NULL);
   bool running = true;
   if( settings.do_fft != 0 ) {
      fft_thread = new std::thread(fft_work_thread, std::ref(server), std::ref(settings), std::ref(running));
   }

   double start = get_monotonic_seconds();
   
   if( settings.do_iq != 0 ) {
      std::ostream* out;
      std::ofstream outfile;
      if(strcmp("-", settings.samples_outfilename) == 0) {
         out = &std::cout;
      } else {
         outfile.open(settings.samples_outfilename, std::ofstream::binary);
         out = &outfile;
      }

   
      
      if(settings.sample_bits == 16) {
         // 16-bit samples
         // each 'sample' is 2 bytes I + 2 bytes Q
         char* data = new char[batch_sz*2*2];
         while(settings.samples == 0 || rxd < settings.samples) {
            rxd += server.get_iq_data(batch_sz,(int16_t*)data);
            out->write(data, batch_sz*2*2);
//            std::cerr << "w16 " << std::flush;
         }
      } else {
         // 8-bit samples
         char* data = new char[batch_sz*2];
         while(settings.samples == 0 || rxd < settings.samples) {
            rxd += server.get_iq_data(batch_sz,(uint8_t*)data);
            out->write(data, batch_sz);
//            std::cerr << "w8 " << std::flush;
         }
      }
      
      if(out != &std::cout) {
         dynamic_cast<std::ofstream*>(out)->close();   
      }

      running = false;
   }
   
  double stop = get_monotonic_seconds();

  if( NULL != fft_thread && fft_thread->joinable() ) {
      fft_thread->join();
   } else {
//      std::cerr << "thread not joinable.\n";
   }
   
   std::cerr << "Received " << rxd << " samples in " << (stop - start)
             << " sec (" << rxd/(stop-start) << " samp/sec)" << std::endl;

   server.stop();
   
   return 0;
}
