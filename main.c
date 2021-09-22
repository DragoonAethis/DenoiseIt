#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <sndfile.h>
#include <rnnoise.h>

#define RNNOISE_SAMPLE_RATE 48000

static struct option long_options[] = {
        { "model", required_argument, 0, 'm' },
        { "amplify", required_argument, 0, 'a' },
        { "prefeed", required_argument, 0, 'p' },
        { 0, 0, 0, 0 }
};

struct channel_state {
    DenoiseState * ds;
    float * input;
    float * output;
};

void print_help() {
    printf("Usage: denoiseit [OPTIONS...] INPUT OUTPUT\n");
    printf("\n");
    printf("Denoise the INPUT audio file with RNNoise and save the result to OUTPUT.\n");
    printf("OUTPUT must have the same extension/file format as INPUT.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -m, --model    Path to the custom RNNoise model\n");
    printf("  -a, --amplify  Amplify the input before denoising (default: 1.0)\n");
    printf("  -p, --prefeed  Number of seconds to read ahead to teach RNNoise\n");
    printf("                 what noise to remove (useful for noisy starts)\n");
    printf("\n");
    printf("More info: https://github.com/DragoonAethis/DenoiseIt\n");
}

int main(int argc, char ** argv) {
    int option_index, option;
    float amplify_factor = 1.0f;
    float prefeed_seconds = 0.0f;
    RNNModel * rnnoise_model = NULL;

    do {
        option = getopt_long(argc, argv, "m:a:p:", long_options, &option_index);
        if (option == -1) break;

        switch (option) {
            case 'm': {
                printf("Trying to use RNNoise model: %s\n", optarg);

                FILE * model_file = fopen(optarg, "r");
                if (model_file == NULL) {
                    printf("Could not read the provided RNNoise model: %s\n", strerror(errno));
                    return -1;
                }

                rnnoise_model = rnnoise_model_from_file(model_file);
                if (rnnoise_model == NULL) {
                    printf("RNNoise could not load the provided file as a valid model.\n");
                    return -1;
                }

                fclose(model_file);
            } break;

            case 'a': {
                amplify_factor = strtof(optarg, NULL);
                if (errno != 0) {
                    printf("Provided amplification factor is not a valid floating point value.\n");
                    return -1;
                }

                printf("Using amplification factor: %f\n", amplify_factor);
            } break;

            case 'p': {
                prefeed_seconds = strtof(optarg, NULL);
                if (errno != 0) {
                    printf("Provided prefeed seconds value is not a valid floating point value.\n");
                    return -1;
                }

                printf("Prefeeding RNNoise with %f seconds of audio\n", amplify_factor);
            } break;

            default: {
                // getopt_long prints an error already, so...
                print_help();
                return -1;
            }
        }
    } while (1);

    int remaining_options = argc - optind;
    if (remaining_options != 2) {
        if (remaining_options < 2) {
            printf("Error: Not enough arguments given.\n");
        } else {
            printf("Error: Too many arguments given.\n");
        }

        print_help();
        return -1;
    }

    const char * input_path = argv[optind];
    const char * output_path = argv[optind + 1];

    // RNNoise can only process single-channel frames with this many samples:
    int rnnoise_frame_size = rnnoise_get_frame_size();

    SF_INFO input_info = {0};
    SNDFILE * input_file = sf_open(input_path, SFM_READ, &input_info);
    if (input_file == NULL) {
        printf("Could not open the input file: %s\n", sf_strerror(input_file));
        return -1;
    }

    if (input_info.seekable == 0) {
        printf("Input file is not seekable and cannot be processed.\n");
        return -1;
    }

    if (input_info.samplerate != RNNOISE_SAMPLE_RATE) {
        printf("Input file sample rate is %dHz, RNNoise can process only %dHz.\n",
               input_info.samplerate, RNNOISE_SAMPLE_RATE);
        printf("(Try using ffmpeg or sox to convert it to WAV first.)\n");
        return -1;

    }

    if (!sf_format_check(&input_info)) {
        printf("Provided input file format cannot be used for writing the output file.\n");
        printf("(Try using ffmpeg or sox to convert it to WAV first.)\n");
        return -1;
    }

    sf_count_t input_frames = sf_seek(input_file, 0, SEEK_END);
    if (input_frames < rnnoise_frame_size) {
        printf("Input file is too short to be processed with RNNoise.\n");
        return -1;
    }

    // Rewind the input to the start:
    sf_seek(input_file, 0, SEEK_SET);

    SF_INFO output_info = {0};
    memcpy(&output_info, &input_info, sizeof(SF_INFO));
    SNDFILE * output_file = sf_open(output_path, SFM_WRITE, &output_info);
    if (output_file == NULL) {
        printf("Could not open the output file: %s\n", sf_strerror(output_file));
        return -1;
    }

    // Create the per-channel denoising state:
    int channels = input_info.channels;
    struct channel_state state[channels];
    for (int ch = 0; ch < channels; ch++) {
        state[ch].ds = rnnoise_create(rnnoise_model);
        state[ch].input = calloc(sizeof(float), rnnoise_frame_size);
        state[ch].output = calloc(sizeof(float), rnnoise_frame_size);
    }

    // Create the multi-channel frame:
    float * full_frame = calloc(sizeof(float), rnnoise_frame_size * channels);

    int is_prefeeding = prefeed_seconds > 0.0f;
    sf_count_t prefeed_frames_left = prefeed_seconds * input_info.samplerate;
    printf("Needs %ld frames for prefeed\n", prefeed_frames_left);

    if (prefeed_frames_left > input_frames) {
        printf("Prefeed exceeds file length - capping to %ld frames\n", input_frames);
        prefeed_frames_left = input_frames;
    }

    sf_count_t current_frame = 0;
    while (current_frame < input_frames) {
        sf_count_t write_from = 0;
        sf_count_t write_frames = rnnoise_frame_size;

        if ((current_frame + rnnoise_frame_size) > input_frames) {
            // Because we're not able to consume the whole frame for RNNoise,
            // sometimes we need to "borrow" some audio from the previous
            // frame and write only the missing trailer.
            write_frames = input_frames - current_frame;

            // For RNNoise frame size = 480, if we have 300 frames to
            // write, we need to start writing to the output from the
            // 180th sndfile frame. This is the full_frame offset:
            write_from = (rnnoise_frame_size - write_frames) * channels;

            // And seek to the (end - frame size) for the actual "borrow":
            sf_seek(input_file, -rnnoise_frame_size, SEEK_END);
        }

        sf_readf_float(input_file, full_frame, rnnoise_frame_size);

        // RNNoise can only operate on a single channel at any given time.
        // Split the read audio into separate channel buffers and process
        // each buffer accordingly:
        for (int ch = 0; ch < channels; ch++) {
            for (int sample = 0; sample < rnnoise_frame_size; sample++) {
                // RNNoise needs really high values for its inputs...
                // Amplify the input here - don't divide by preamp later.
                state[ch].input[sample] = full_frame[ch + (sample * channels)] * 32768.0f * amplify_factor;
            }

            rnnoise_process_frame(state[ch].ds, state[ch].output, state[ch].input);
            bzero(state[ch].input, rnnoise_frame_size * sizeof(float));

            if (!is_prefeeding) {
                // We still need the original audio, don't denoise the prefed period twice!
                for (int sample = 0; sample < rnnoise_frame_size; sample++) {
                    full_frame[ch + (sample * channels)] = state[ch].output[sample] / 32768.0f;
                }
            }

        }

        if (!is_prefeeding) {
            // At this point, full_frame contains denoised audio (hopefully).
            // Write it to the output and move our frame pointer forward.
            if (sf_writef_float(output_file, full_frame+write_from, write_frames) != write_frames) {
                // Out of disk space? Something else?
                printf("Failed to write enough frames to the output file!\n");
                return -1;
            }
        } else {
            prefeed_frames_left -= write_frames;
            if (prefeed_frames_left <= 0) {
                // Disable prefeed, rewind the input file, do the actual thing!
                printf("Prefeed completed\n");
                is_prefeeding = 0;
                prefeed_frames_left = 0;

                current_frame = 0;
                sf_seek(input_file, 0, SEEK_SET);

                continue; // Don't bump current frames below at this point.
            }
        }

        current_frame += write_frames;
    }

    // And we're done - audio denoised, output written. Close everything cleanly.

    for (int ch = 0; ch < channels; ch++) {
        rnnoise_destroy(state[ch].ds);
        free(state[ch].output);
        free(state[ch].input);
    }

    free(full_frame);

    sf_close(output_file);
    sf_close(input_file);

    if (rnnoise_model != NULL) {
        rnnoise_model_free(rnnoise_model);
    }

    printf("Done, processed %ld frames.\n", current_frame);
    return 0;
}
