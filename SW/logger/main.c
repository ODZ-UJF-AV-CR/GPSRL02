
/*
 * Copyright (c) 2006-2007 by Roland Riegel <feedback@roland-riegel.de>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "fat16.h"
#include "fat16_config.h"
#include "partition.h"
#include "sd_raw.h"
#include "sd_raw_config.h"
#include "uart.h"

#define DEBUG 1


static uint8_t read_line(char* buffer, uint8_t buffer_length);
static uint32_t strtolong(const char* str);
static uint8_t find_file_in_dir(struct fat16_fs_struct* fs, struct fat16_dir_struct* dd, const char* name, struct fat16_dir_entry_struct* dir_entry);
static struct fat16_file_struct* open_file_in_dir(struct fat16_fs_struct* fs, struct fat16_dir_struct* dd, const char* name); 
static uint8_t print_disk_info(const struct fat16_fs_struct* fs);

int main()
{
    /* we will just use ordinary idle mode */
    set_sleep_mode(SLEEP_MODE_IDLE);

    /* setup uart */
    uart_init();

    /* setup sd card slot */
    if(!sd_raw_init())
    {
#if DEBUG
        uart_puts_p(PSTR("MMC/SD initialization failed\n"));
#endif
        return 1;
    }

    /* open first partition */
    struct partition_struct* partition = partition_open(sd_raw_read,
                                                        sd_raw_read_interval,
                                                        sd_raw_write,
                                                        sd_raw_write_interval,
                                                        0
                                                       );

    if(!partition)
    {
        /* If the partition did not open, assume the storage device
         * is a "superfloppy", i.e. has no MBR.
         */
        partition = partition_open(sd_raw_read,
                                   sd_raw_read_interval,
                                   sd_raw_write,
                                   sd_raw_write_interval,
                                   -1
                                  );
        if(!partition)
        {
#if DEBUG
            uart_puts_p(PSTR("opening partition failed\n"));
#endif
            return 1;
        }
    }

    /* open file system */
    struct fat16_fs_struct* fs = fat16_open(partition);
    if(!fs)
    {
#if DEBUG
        uart_puts_p(PSTR("opening filesystem failed\n"));
#endif
        return 1;
    }

    /* open root directory */
    struct fat16_dir_entry_struct directory;
    fat16_get_dir_entry_of_path(fs, "/", &directory);

    struct fat16_dir_struct* dd = fat16_open_dir(fs, &directory);
    if(!dd)
    {
#if DEBUG
        uart_puts_p(PSTR("opening root directory failed\n"));
#endif
        return 1;
    }
    
    /* print some card information as a boot message */
    print_disk_info(fs);

    /* provide a simple shell */
    char buffer[20];
    char* command = buffer;

//!!!KAKL
{
    uint8_t n;
 	
    while(uart_getc()!='$');
    while(uart_getc()!=',');
    for(n=0; n<6; n++)
    {
	buffer[n]=uart_getc();
    };
    buffer[6]='\0';
}

{		
    struct fat16_dir_entry_struct file_entry;
    fat16_create_file(dd, command, &file_entry);
}

{ 
    int32_t offset;

    offset = 0;
    while(1)
    {	
	uint8_t znak;

	struct fat16_file_struct* fd = open_file_in_dir(fs, dd, command);
	fat16_seek_file(fd, &offset, FAT16_SEEK_SET);
	do
	{	
	   znak=uart_getc();
	   fat16_write_file(fd, (uint8_t*) &znak, 1);
	   uart_putc(znak);	
	   offset++;
	} while ((znak!='\n')&&(znak!='@'));	

        fat16_close_file(fd);

	if(znak=='@') break;
    }
}

    while(1)
    {
        /* print prompt */
        uart_putc('>');
        uart_putc(' ');

        /* read command */
        if(read_line(command, sizeof(buffer)) < 1)
            continue;

        /* execute command */
        if(strncmp_P(command, PSTR("cd "), 3) == 0)
        {
            command += 3;
            if(command[0] == '\0')
                continue;

            /* change directory */
            struct fat16_dir_entry_struct subdir_entry;
            if(find_file_in_dir(fs, dd, command, &subdir_entry))
            {
                struct fat16_dir_struct* dd_new = fat16_open_dir(fs, &subdir_entry);
                if(dd_new)
                {
                    fat16_close_dir(dd);
                    dd = dd_new;
                    continue;
                }
            }

            uart_puts_p(PSTR("directory not found: "));
            uart_puts(command);
            uart_putc('\n');
        }
        else if(strcmp_P(command, PSTR("ls")) == 0)
        {
            /* print directory listing */
            struct fat16_dir_entry_struct dir_entry;
            while(fat16_read_dir(dd, &dir_entry))
            {
                uint8_t spaces = sizeof(dir_entry.long_name) - strlen(dir_entry.long_name) + 4;

                uart_puts(dir_entry.long_name);
                uart_putc(dir_entry.attributes & FAT16_ATTRIB_DIR ? '/' : ' ');
                while(spaces--)
                    uart_putc(' ');
                uart_putdw_dec(dir_entry.file_size);
                uart_putc('\n');
            }
        }
        else if(strncmp_P(command, PSTR("cat "), 4) == 0)
        {
            command += 4;
            if(command[0] == '\0')
                continue;
            
            /* search file in current directory and open it */
            struct fat16_file_struct* fd = open_file_in_dir(fs, dd, command);
            if(!fd)
            {
                uart_puts_p(PSTR("error opening "));
                uart_puts(command);
                uart_putc('\n');
                continue;
            }

            /* print file contents */
            uint8_t buffer[8];
            uint32_t offset = 0;
            while(fat16_read_file(fd, buffer, sizeof(buffer)) > 0)
            {
                uart_putdw_hex(offset);
                uart_putc(':');
                for(uint8_t i = 0; i < 8; ++i)
                {
                    uart_putc(' ');
                    uart_putc_hex(buffer[i]);
                }
                uart_putc('\n');
                offset += 8;
            }

            fat16_close_file(fd);
        }
        else if(strcmp_P(command, PSTR("disk")) == 0)
        {
            if(!print_disk_info(fs))
                uart_puts_p(PSTR("error reading disk info\n"));
        }
#if FAT16_WRITE_SUPPORT
        else if(strncmp_P(command, PSTR("rm "), 3) == 0)
        {
            command += 3;
            if(command[0] == '\0')
                continue;
            
            struct fat16_dir_entry_struct file_entry;
            if(find_file_in_dir(fs, dd, command, &file_entry))
            {
                if(fat16_delete_file(fs, &file_entry))
                    continue;
            }

            uart_puts_p(PSTR("error deleting file: "));
            uart_puts(command);
            uart_putc('\n');
        }
        else if(strncmp_P(command, PSTR("touch "), 6) == 0)
        {
            command += 6;
            if(command[0] == '\0')
                continue;

            struct fat16_dir_entry_struct file_entry;
            if(!fat16_create_file(dd, command, &file_entry))
            {
                uart_puts_p(PSTR("error creating file: "));
                uart_puts(command);
                uart_putc('\n');
            }
        }
        else if(strncmp_P(command, PSTR("write "), 6) == 0)
        {
            command += 6;
            if(command[0] == '\0')
                continue;

            char* offset_value = command;
            while(*offset_value != ' ' && *offset_value != '\0')
                ++offset_value;

            if(*offset_value == ' ')
                *offset_value++ = '\0';
            else
                continue;

            /* search file in current directory and open it */
            struct fat16_file_struct* fd = open_file_in_dir(fs, dd, command);
            if(!fd)
            {
                uart_puts_p(PSTR("error opening "));
                uart_puts(command);
                uart_putc('\n');
                continue;
            }

            int32_t offset = strtolong(offset_value);
            if(!fat16_seek_file(fd, &offset, FAT16_SEEK_SET))
            {
                uart_puts_p(PSTR("error seeking on "));
                uart_puts(command);
                uart_putc('\n');

                fat16_close_file(fd);
                continue;
            }

            /* read text from the shell and write it to the file */
            uint8_t data_len;
            while(1)
            {
                /* give a different prompt */
                uart_putc('<');
                uart_putc(' ');

                /* read one line of text */
                data_len = read_line(buffer, sizeof(buffer));
                if(!data_len)
                    break;

                /* write text to file */
                if(fat16_write_file(fd, (uint8_t*) buffer, data_len) != data_len)
                {
                    uart_puts_p(PSTR("error writing to file\n"));
                    break;
                }
            }

            fat16_close_file(fd);
        }
        else if(strncmp_P(command, PSTR("mkdir "), 6) == 0)
        {
            command += 6;
            if(command[0] == '\0')
                continue;

            struct fat16_dir_entry_struct dir_entry;
            if(!fat16_create_dir(dd, command, &dir_entry))
            {
                uart_puts_p(PSTR("error creating directory: "));
                uart_puts(command);
                uart_putc('\n');
            }
        }
#endif
#if SD_RAW_WRITE_BUFFERING
        else if(strcmp_P(command, PSTR("sync")) == 0)
        {
            if(!sd_raw_sync())
                uart_puts_p(PSTR("error syncing disk\n"));
        }
#endif
        else
        {
            uart_puts_p(PSTR("unknown command: "));
            uart_puts(command);
            uart_putc('\n');
        }
    }

    /* close file system */
    fat16_close(fs);

    /* close partition */
    partition_close(partition);
    
    return 0;
}

uint8_t read_line(char* buffer, uint8_t buffer_length)
{
    memset(buffer, 0, buffer_length);

    uint8_t read_length = 0;
    while(read_length < buffer_length - 1)
    {
        uint8_t c = uart_getc();

        if(c == 0x08 || c == 0x7f)
        {
            if(read_length < 1)
                continue;

            --read_length;
            buffer[read_length] = '\0';

            uart_putc(0x08);
            uart_putc(' ');
            uart_putc(0x08);

            continue;
        }

        uart_putc(c);

        if(c == '\n')
        {
            buffer[read_length] = '\0';
            break;
        }
        else
        {
            buffer[read_length] = c;
            ++read_length;
        }
    }

    return read_length;
}

uint32_t strtolong(const char* str)
{
    uint32_t l = 0;
    while(*str >= '0' && *str <= '9')
        l = l * 10 + (*str++ - '0');

    return l;
}

uint8_t find_file_in_dir(struct fat16_fs_struct* fs, struct fat16_dir_struct* dd, const char* name, struct fat16_dir_entry_struct* dir_entry)
{
    while(fat16_read_dir(dd, dir_entry))
    {
        if(strcmp(dir_entry->long_name, name) == 0)
        {
            fat16_reset_dir(dd);
            return 1;
        }
    }

    return 0;
}

struct fat16_file_struct* open_file_in_dir(struct fat16_fs_struct* fs, struct fat16_dir_struct* dd, const char* name)
{
    struct fat16_dir_entry_struct file_entry;
    if(!find_file_in_dir(fs, dd, name, &file_entry))
        return 0;

    return fat16_open_file(fs, &file_entry);
}

uint8_t print_disk_info(const struct fat16_fs_struct* fs)
{
    if(!fs)
        return 0;

    struct sd_raw_info disk_info;
    if(!sd_raw_get_info(&disk_info))
        return 0;

    uart_puts_p(PSTR("manuf:  0x")); uart_putc_hex(disk_info.manufacturer); uart_putc('\n');
    uart_puts_p(PSTR("oem:    ")); uart_puts((char*) disk_info.oem); uart_putc('\n');
    uart_puts_p(PSTR("prod:   ")); uart_puts((char*) disk_info.product); uart_putc('\n');
    uart_puts_p(PSTR("rev:    ")); uart_putc_hex(disk_info.revision); uart_putc('\n');
    uart_puts_p(PSTR("serial: 0x")); uart_putdw_hex(disk_info.serial); uart_putc('\n');
    uart_puts_p(PSTR("date:   ")); uart_putw_dec(disk_info.manufacturing_month); uart_putc('/');
                                   uart_putw_dec(disk_info.manufacturing_year); uart_putc('\n');
    uart_puts_p(PSTR("size:   ")); uart_putdw_dec(disk_info.capacity); uart_putc('\n');
    uart_puts_p(PSTR("copy:   ")); uart_putw_dec(disk_info.flag_copy); uart_putc('\n');
    uart_puts_p(PSTR("wr.pr.: ")); uart_putw_dec(disk_info.flag_write_protect_temp); uart_putc('/');
                                   uart_putw_dec(disk_info.flag_write_protect); uart_putc('\n');
    uart_puts_p(PSTR("format: ")); uart_putw_dec(disk_info.format); uart_putc('\n');
    uart_puts_p(PSTR("free:   ")); uart_putdw_dec(fat16_get_fs_free(fs)); uart_putc('/');
                                   uart_putdw_dec(fat16_get_fs_size(fs)); uart_putc('\n');

    return 1;
}

void get_datetime(uint16_t* year, uint8_t* month, uint8_t* day, uint8_t* hour, uint8_t* min, uint8_t* sec)
{
    *year = 2007;
    *month = 1;
    *day = 1;
    *hour = 0;
    *min = 0;
    *sec = 0;
}


