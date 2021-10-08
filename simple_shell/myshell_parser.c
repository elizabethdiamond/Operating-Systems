#include "myshell_parser.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define SPECIAL_CHARS "<>|&"

static struct pipeline_command* pipeline_command_alloc()
{
		struct pipeline_command* command =
						malloc(sizeof(struct pipeline_command));
		if (command) {
						command->next = NULL;
						for(int i = 0; i < MAX_ARGV_LENGTH; i++)
						{
								command->command_args[i] = NULL;
						}
						command->redirect_in_path = NULL;
						command->redirect_out_path = NULL;
		}
		return command;
}

/* tokenize the command line by special chars and whitespaces */
char** tokenize(const char* command_line) {
		/* writeable copy of the command line for tokenization*/
		char* command_buf = strdup(command_line);
		char* cl = command_buf;
		size_t line_length = strlen(command_line);
		/* get tokens */
		char** tokens = malloc(MAX_ARGV_LENGTH * MAX_LINE_LENGTH);
		char* curr_token = malloc(sizeof(char) * line_length);
		memset(curr_token, 0, line_length);
		char copy[2];
		copy[1] = '\0';
		char terminate[1];
		terminate[0] = '\0';
		size_t token_length = 0;
		int i = 0;
		int j = 0;

		for(i = 0; i < line_length; i++) {
					char curr_char = cl[i];
					copy[0] = curr_char;
					token_length = strlen(curr_token);
					if (isspace(curr_char)) {
						if (token_length > 0) {
							char* temp = malloc(sizeof(char) * token_length + 1);
							memcpy(temp, &curr_token[0], token_length + 1);
							tokens[j++] = temp;
							memset(curr_token, 0, token_length + 1);
						}
					}
					else if (strspn(&curr_char, SPECIAL_CHARS) != 0) {
						if (token_length > 0) {
							char* temp = malloc(sizeof(char) * token_length + 1);
							memcpy(temp, &curr_token[0], token_length + 1);
							tokens[j++] = temp;
							memset(&curr_token[0], 0, token_length + 1);
							char* temp2 = malloc(sizeof(char*));
							memcpy(temp2, copy, 8);
							tokens[j++] = temp2;
						}
						else {
							char* temp3 = malloc(sizeof(char*));
							memcpy(temp3, copy, 8);
							tokens[j++] = temp3;
						}
					}
					else {
							strcat(curr_token, copy);
							strcat(curr_token, terminate);
					}
		}
		return tokens;
}

char* get_next_token(char** tokens, int i) {
		char* next_token = tokens[i+1];
		if (strlen(next_token) <= 1) {
				perror("ERROR");
				return NULL;
		}
		return next_token;
}


struct pipeline* pipeline_build(const char* command_line)
{
		struct pipeline* pipeline = malloc(sizeof(struct pipeline));
		pipeline->is_background = false;

		if (sizeof(command_line) > MAX_LINE_LENGTH) {
				fprintf(stderr, "ERROR: Input command line too long.\n");
				return NULL;
		}

		/* tokenize the command line (free malloc tokens) */
		char** tokens = tokenize(command_line);
		/* parse the tokenized command line into pipeline struct */
		pipeline->commands = pipeline_command_alloc();
		struct pipeline_command* curr_command = pipeline->commands;
		struct pipeline_command* next_command;
		int arg_count = 0;
		int i = 0;

		while (tokens[i] != NULL) {
				if (strcmp(tokens[i], "&") == 0) {
						pipeline->is_background = true;
				}
				else if (strcmp(tokens[i], "<") == 0) {
						curr_command->redirect_in_path = get_next_token(tokens, i);
						i++;
				}
				else if (strcmp(tokens[i], ">") == 0) {
						curr_command->redirect_out_path = get_next_token(tokens, i);
						i++;
				}
				else if (strcmp(tokens[i], "|") == 0) {
						if (arg_count == 0) {
								fprintf(stderr, "ERROR: Pipe before a command.\n");
								return NULL;
						}
						next_command = pipeline_command_alloc();
						curr_command->next = next_command;
						curr_command = next_command;
						arg_count = 0;
				}
				else {
						curr_command->command_args[arg_count] = tokens[i];
						arg_count++;
						curr_command->command_args[arg_count] = NULL;
				}
				i++;
		}

		free(tokens);
		return pipeline;
}



void pipeline_free(struct pipeline* pipeline)
{
		/* free pipeline_command_args */
		for(int i = 0; i < MAX_ARGV_LENGTH; i++) {
				//NULL is the end of command_args
				if (pipeline->commands->command_args[i] == NULL)
				 		break;
				free(pipeline->commands->command_args[i]);
		}
		/* free rest of pipeline_command */
		if (pipeline->commands->redirect_in_path)
				free(pipeline->commands->redirect_in_path);
		if (pipeline->commands->redirect_out_path)
				free(pipeline->commands->redirect_out_path);
		free(pipeline);
}
