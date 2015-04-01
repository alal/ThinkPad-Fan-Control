/*
ThinkPad fan control
Copyright 2008, Stanko Tadić <stanko@mfhinc.net>

ThinkPad fan control is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.
Note that I am not granting permission to redistribute or
modify ThinkPad fan control under the terms of any later version of the
General Public License.

ThinkPad fan control is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program (in the file "LICENCE"); if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
MA 02111, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>

#define AUTO 10  // auto in auto mode
#define FORCED 100  // critical in auto mode

#define AUTO_SPEED 8
#define FULL_SPEED 9

#define MIN_SANE_CRITICAL_SPEED 3

/* location of UI XML file relative to path in which program is running */
#define BUILDER_XML_FILE "data/gtk_gui.xml"

// global GTK widgets
GtkSpinButton *sleepField, *criticalField, *safeField;
GtkComboBox *autoSpeed, *speedValue;
GtkStatusbar *statusbar;
GtkLabel *level, *txtLog, *optionsLabel;
GtkWidget *window;
GtkButton *run, *exitButton, *hide, *speedButton;
GtkStatusIcon *tray_icon;

// global variables
int visible = 1, manual=0;
int fansLevel = AUTO;
int sleepTime = 10, criticalTemp = 55, safeTemp = 45, autoSpeedValue = 7;
gint timeout;

/****** FUNCTIONS *******/
// when program is closed, turns fans to auto and write that in terminal
void on_window_destroy ();
// main function for controlling fans
int fans();
//changes speed to value 'speed'
void change_speed(int speed);
// when run button is clicked
void run_clicked();
// changes fans speed when user want that
void manual_change ();
// hides window when tray icon is clicked
void hide_window(GtkStatusIcon *status_icon, gpointer user_data);
// popup menu, right button on tray icon
void tray_icon_on_menu(GtkStatusIcon *status_icon, guint button, guint activate_time, gpointer user_data);
// creates tray icon
static GtkStatusIcon *create_tray_icon();

//------------------------------------ MAIN ---------------------------------------------//
int main (int argc, char *argv[]){

    GtkBuilder *builder;

    gtk_init (&argc, &argv);

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, BUILDER_XML_FILE, NULL);

    // making tray icon
    tray_icon = create_tray_icon();

    // getting all the widgets
    window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
    run = GTK_BUTTON (gtk_builder_get_object (builder, "run"));
    exitButton = GTK_BUTTON (gtk_builder_get_object (builder, "exit"));
    speedButton = GTK_BUTTON (gtk_builder_get_object (builder, "speedButton"));
    hide = GTK_BUTTON (gtk_builder_get_object (builder, "hide"));
    sleepField = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "sleep"));
    criticalField = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "critical"));
    safeField = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "safe"));
    txtLog = GTK_LABEL (gtk_builder_get_object (builder, "log"));
    optionsLabel = GTK_LABEL (gtk_builder_get_object (builder, "optionsLabel"));
    statusbar = GTK_STATUSBAR (gtk_builder_get_object (builder, "statusbar"));
    level = GTK_LABEL (gtk_builder_get_object (builder, "level"));
    speedValue = GTK_COMBO_BOX (gtk_builder_get_object (builder, "speed"));
    autoSpeed = GTK_COMBO_BOX (gtk_builder_get_object (builder, "autoSpeed"));

    // combo boxes default values
    gtk_combo_box_set_active(autoSpeed,7);
    gtk_combo_box_set_active(speedValue,0);

    // callbacks
    // when clicked on run button
    g_signal_connect(G_OBJECT(run), "clicked", G_CALLBACK(run_clicked), NULL);
    // when clicked on exit button
    g_signal_connect(G_OBJECT(exitButton), "clicked", G_CALLBACK(on_window_destroy), NULL);
    // when clicked on to tray button
    g_signal_connect(G_OBJECT(hide), "clicked", G_CALLBACK(hide_window), NULL);
    // when clicked on manual change button
    g_signal_connect(G_OBJECT(speedButton), "clicked", G_CALLBACK(manual_change), NULL);
    // on window destroy
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_window_destroy), NULL);

    gtk_builder_connect_signals (builder, NULL);
    g_object_unref (G_OBJECT (builder));

    gtk_widget_show (window);

    // welcome terminal message, and changing fans control to auto at startup
    system("echo level auto > /proc/acpi/ibm/fan");
    printf("** Welcome! **\n");
    printf("** ThinkPad fan control by Stanko! **\n");
    printf("** Fan control changed to auto **\n");


    // starting fans function with default values (global variables)
    fans();
    // setting sleep time (timeout) for fans function, also default value (global)
    timeout = g_timeout_add_seconds(sleepTime,fans,NULL);

    gtk_main ();

    return 0;
}

//-------------------------------------- FANS ---------------------------------------------//
int fans(){
    // tepmerature of CPU
    int temp;
    // file pointer on file in which is CPU temperature, and second for popen for getting time from terminal
    FILE *tempInput, *sysIn;
    // status and other messages
    char message[80];
    char tempMessage[120];
    char tmpString[45];

    // wrong options
    if(safeTemp >= criticalTemp){
        safeTemp = criticalTemp - 5;
        printf("** safeTemp change to %d because of higher than criticalTemp", safeTemp);
    }

    // reading the CPU temperature
    // may need change to "/sys/bus/platform/devices/thinkpad_hwmon/temp1_input"(
    // something like "47000" inside) if the following is deprecated.
    tempInput = fopen("/proc/acpi/ibm/thermal","r");
    if(tempInput ==NULL){
        gtk_label_set_text(txtLog,"YOU ARE NOT RUNNING KERNEL WITH THINKPAD PATCH!");
        return FALSE;
    }
    fscanf(tempInput,"temperatures:	%d",&temp);
    fclose(tempInput);

    // getting time
    sysIn = popen("date '+%H:%M:%S'","r");
    fgets(tmpString,9,sysIn);
    pclose(sysIn);
    // writing CPU temperature, and time when it's checked
    sprintf(message,"Temperature: %dC, Checked at %s",temp,tmpString);
    gtk_statusbar_push(statusbar,0,message);

    // if safe temperature is reached.
    if(temp<=safeTemp){
        fansLevel = AUTO;
    }
    // if critical temperature is reached.
    if(temp>=criticalTemp){
        // changing fans level to one user choosed
        change_speed(autoSpeedValue);
        fansLevel = FORCED;
        // putting log message
        sprintf(tempMessage,"\n%s\nTemperature is %d, critical is %d\nCritical mode in action!", tmpString, temp, criticalTemp);
        gtk_label_set_text(txtLog, tempMessage);
    }
    else if(temp<criticalTemp && fansLevel==AUTO){
        if(manual){
            int speed = gtk_combo_box_get_active (speedValue);
            change_speed(speed);
        }
        else{
            change_speed(AUTO_SPEED);
        }
        fansLevel = AUTO;
        // putting log message
        sprintf(tempMessage,"\n%s\nTemperature is %d, critical is %d\nAuto/Manual is working.", tmpString, temp, criticalTemp);
        gtk_label_set_text(txtLog, tempMessage);
    }
    else if(temp<criticalTemp && fansLevel==FORCED){
        sprintf(tempMessage,"\n%s\nTemperature is %d, safe is %d\nWaitting to reach safe!", tmpString, temp, safeTemp);
        gtk_label_set_text(txtLog, tempMessage);
    }
    return TRUE;
}

//------------------------------------ RUN ---------------------------------------------//
void run_clicked(){
    char tmpString[100];
    char speedString[15];
    // getparameters
    criticalTemp = gtk_spin_button_get_value_as_int (criticalField);
    safeTemp = gtk_spin_button_get_value_as_int (safeField);
    autoSpeedValue = gtk_combo_box_get_active (autoSpeed);

    if (autoSpeedValue < MIN_SANE_CRITICAL_SPEED){
        autoSpeedValue = MIN_SANE_CRITICAL_SPEED;
        printf("** autoSpeedValue change to %d because of lower than MIN_SANE_CRITICAL_SPEED", autoSpeedValue);
    }

    sleepTime = gtk_spin_button_get_value_as_int (sleepField);
    fans();

    if(manual == 1){
        manual = 0;
        gtk_statusbar_push(statusbar,0,"Manual control is off!");
        gtk_button_set_label(run,"Change options");
    }
    else{
        gtk_timeout_remove(timeout);
        if(fansLevel==FORCED)
            change_speed(autoSpeedValue);
        timeout = g_timeout_add_seconds(sleepTime,fans,NULL);

        switch(autoSpeedValue){
            case AUTO_SPEED: strcpy(speedString,"auto"); break;
            case FULL_SPEED: strcpy(speedString,"full-speed"); break;
            default: sprintf(speedString,"%d",autoSpeedValue);
        }
        gtk_button_set_label(run,"Change options");
        sprintf(tmpString,"Current options: %d, %d, %d, %s",sleepTime,criticalTemp,safeTemp,speedString);
        gtk_label_set_text(optionsLabel,tmpString);
    }
}

//------------------------------------ EXIT ---------------------------------------------//
void on_window_destroy (){
    system("echo level auto > /proc/acpi/ibm/fan");
    printf("** Fan control changed to auto **\n");
    printf("** Goodbye! **\n");
    gtk_main_quit();
}

//------------------------------------ TRAY ---------------------------------------------//
void hide_window(GtkStatusIcon *status_icon, gpointer user_data){
    if(visible){
        gtk_widget_hide(window);
        visible=0;
    }
    else{
        gtk_widget_show(window);
        visible=1;
    }
}

void tray_icon_on_menu(GtkStatusIcon *status_icon, guint button, guint activate_time, gpointer user_data){
    printf("Popup menu\n");
}

//------------------------------------ STATUS ICON ---------------------------------------------//
static GtkStatusIcon *create_tray_icon() {
    GtkStatusIcon *tray_icon;

    tray_icon = gtk_status_icon_new();
    g_signal_connect(G_OBJECT(tray_icon), "activate", G_CALLBACK(hide_window), NULL);
    g_signal_connect(G_OBJECT(tray_icon), "popup-menu",G_CALLBACK(tray_icon_on_menu), NULL);
    gtk_status_icon_set_from_file(tray_icon, "data/icon.png");
    gtk_status_icon_set_tooltip(tray_icon, "ThinkPad Fan Control .:: level - auto ::.");
    gtk_status_icon_set_visible(tray_icon, TRUE);

    return tray_icon;
}
//------------------------------------ MANUAL CHANGE ---------------------------------------------//
void manual_change (){
    manual = 1;
    gtk_button_set_label(run,"Run auto control");
    gtk_statusbar_push(statusbar,0,"Manual control is active!");
}

//------------------------------------ CHANGE SPEED ---------------------------------------------//
void change_speed(int speed){
    char tmpString[60];
    char speedString[15];

    switch(speed){
        case AUTO_SPEED: strcpy(speedString,"auto"); break;
        case FULL_SPEED: strcpy(speedString,"full-speed"); break;
        default: sprintf(speedString,"%d",speed);
    }
    sprintf(tmpString,"echo level %s > /proc/acpi/ibm/fan",speedString);
    system(tmpString);
    sprintf(tmpString,"Fans level - %s",speedString);
    gtk_label_set_text(level,tmpString);

    sprintf(tmpString,"ThinkPad Fan Control .:: level - %s ::.",speedString);
    gtk_status_icon_set_tooltip(tray_icon, tmpString);

    printf("** Speed level changed to %s **\n",speedString);
}
