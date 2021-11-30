#include<unistd.h>
#include<stdio.h>
#include<sys/mman.h>
#include<pthread.h>
#include<semaphore.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>

int irq_pipe[2];
int ack_pipe[2];
int ctl_pipe[2];
int* led_buff;
int open_status=0;
int input_stopped=0;
pthread_t kbd_core_irq;
pthread_t kbd_core_ctl;
sem_t status_lock;
struct my_kbd_urb;
typedef void* (*handler_routine)(void* urb);

struct my_kbd_urb{
    char* data_buf;
    int fd_pipe;
    handler_routine handle;
    char* previous;
};
struct my_kbd_dev{
    struct my_kbd_urb* urb_irq;
    struct my_kbd_urb* urb_led;
    //sem_t led_buff_lock;
    sem_t led_urb_lock;
    int caps_mode;//0---->small case
    bool irq_urb_submitted,led_urb_submitted;
    int pending_led_cmd;
    sem_t submit_led;
};

struct my_kbd_dev* my_dev;
int submit_urb(struct my_kbd_urb* my_urb_irq, struct my_kbd_urb* my_urb_led);

void* poll_irq_func(void*my_urb_irq){
    //printf("polling irq\n");
    struct my_kbd_urb* my_urb_irq1=(struct my_kbd_urb*)my_urb_irq;
    //printf("%c\n",*my_urb_irq1->data_buf);
    pthread_t irq_handle_thread;
    while(read(my_urb_irq1->fd_pipe,my_urb_irq1->data_buf,1)>0){
        //printf("in driver %c\n",*my_urb_irq1->data_buf);
        if(pthread_create(&irq_handle_thread,NULL,my_urb_irq1->handle,(void*)my_urb_irq)<0){
            printf("irq_handle_thread not created\n");
            return 0;
        }
        pthread_join(irq_handle_thread,NULL);
    }
    //printf("nothing to read\n");
    close(my_urb_irq1->fd_pipe);
    printf("\n");
    sleep(0.5);
    exit(0);
}
void* poll_ack_func(void*my_urb_led){
    //sleep(10);
    //printf("polling ack\n");
    struct my_kbd_urb* my_urb_led1=(struct my_kbd_urb*)my_urb_led;
    //printf("polling ack\n");
    char c='C';
    char* alert=&c;
    while(1){
        sem_wait(&(my_dev->submit_led));
        //printf("sending ctl %c\n",c);
        write(ctl_pipe[1],alert,1);
        pthread_t led_handle_thread;
        if(read(my_urb_led1->fd_pipe,my_urb_led1->data_buf,1)>0){
            if(pthread_create(&led_handle_thread,NULL,my_urb_led1->handle,(void*)my_urb_led)<0){
                printf("led_handle_thread not created\n");
                return 0;
            }
            pthread_join(led_handle_thread,NULL);
        }
        else{
            break;
        }
    }
    close(ctl_pipe[1]);
    close(my_urb_led1->fd_pipe);
    return 0;
}

void* usb_event_func(){
	//printf("in event func\n");
	sem_wait(&my_dev->led_urb_lock);
    if(my_dev->led_urb_submitted==true){
        my_dev->pending_led_cmd+=1;
        sem_post(&my_dev->led_urb_lock);
        return;
    }
    *led_buff=(*led_buff+1)%2;
    sem_post(&my_dev->led_urb_lock);
    submit_urb(NULL,my_dev->urb_led);
    return;
}

void input_report_key(char key){
    //printf("in input key %c\n",key);
    if(isalnum(key)){
    	//printf("%c\n",*my_dev->urb_irq->previous);
        if(*(my_dev->urb_irq->previous)=='@'){
            printf("%c",*my_dev->urb_irq->previous);
            *my_dev->urb_irq->previous='\0';
        }
        if(my_dev->caps_mode==1){
            printf("%c",toupper(key));
            return;
        }
        printf("%c",key);
        return;
    }
    if(key=='#'){
        if(*my_dev->urb_irq->previous=='@'){
            printf("%c",*my_dev->urb_irq->previous);
            my_dev->urb_irq->previous='\0';
        }
        return;
    }
    if(key=='@'){
        if(*my_dev->urb_irq->previous=='@'){
            printf("%c",*my_dev->urb_irq->previous);
            my_dev->urb_irq->previous="\0";
        }
        *my_dev->urb_irq->previous='@';
        return;
    }
    if(key=='&'){
        if(*my_dev->urb_irq->previous=='@'){
            my_dev->caps_mode=(my_dev->caps_mode+1)%2;
            *my_dev->urb_irq->previous='\0';
            pthread_t usb_event;
            if(pthread_create(&usb_event,NULL,usb_event_func,NULL)<0){
                printf("event thread not created\n");
            }
            return;
        }
        printf("%c\n",key);
    }
    return; 
}
int submit_urb(struct my_kbd_urb* my_urb_irq, struct my_kbd_urb* my_urb_led){
    //printf("In submit\n");
    sem_wait(&status_lock);
    if(open_status==0){
        open_status=1;
        sem_post(&status_lock);
        //pthread_t kbd_core_irq;
        if(pthread_create(&kbd_core_irq,NULL,poll_irq_func,(void*)my_urb_irq)<0){
            printf("poll_irq_func thread not created\n");
            return 0;
        }
        //pthread_t kbd_core_ctl;
        if(pthread_create(&kbd_core_ctl,NULL,poll_ack_func,(void*)my_urb_led)<0){
            printf("poll_ack_func thread not created \n");
            return 0;
        }
        pthread_join(kbd_core_irq,NULL);
        pthread_join(kbd_core_ctl,NULL);
        exit(0);
    }
    sem_post(&status_lock);
    sem_wait(&my_dev->led_urb_lock);
    my_dev->led_urb_submitted=true;
    sem_post(&my_dev->led_urb_lock);
    sem_post(&my_dev->submit_led);
    return 0;
}
void* irq_handler(void* my_urb_irq){
    struct my_kbd_urb* my_urb_irq1=(struct my_kbd_urb*)my_urb_irq;
    //printf("in irq handler %c\n",*my_urb_irq1->data_buf);
    input_report_key(*my_urb_irq1->data_buf);
    return 0;
}
void* led_handler(void* my_urb_led){
    //printf("acknowledge fire\n");
    sem_wait(&my_dev->led_urb_lock);
    if(my_dev->pending_led_cmd==0){
        my_dev->led_urb_submitted=false;
        sem_post(&my_dev->led_urb_lock);
    }
    else{
        *led_buff=(*led_buff+1)%2;
        my_dev->pending_led_cmd--;
        sem_post(&my_dev->led_urb_lock);
        submit_urb(NULL,my_dev->urb_led);
    }
    return 0;
}
int open_my_kbd(){
    //initialize 2 urbs and submit it.urb will be some data
    //structure with fields irq_data and function handler
    //printf("In open\n");
    my_dev=(struct my_kbd_dev*)malloc(sizeof(struct my_kbd_dev));
    my_dev->urb_irq=(struct my_kbd_urb*)malloc(sizeof(struct my_kbd_urb));
    my_dev->urb_led=(struct my_kbd_urb*)malloc(sizeof(struct my_kbd_urb));
    my_dev->urb_irq->data_buf=(char*)malloc(sizeof(char));
    my_dev->urb_led->data_buf=(char*)malloc(sizeof(char));
    *my_dev->urb_irq->data_buf='m';
    my_dev->urb_irq->handle=&irq_handler;
    my_dev->urb_irq->previous=(char*)malloc(sizeof(char));
    *my_dev->urb_irq->previous='\0';
    my_dev->urb_irq->fd_pipe=irq_pipe[0];
    my_dev->urb_led->fd_pipe=ack_pipe[0];
    my_dev->urb_led->handle=&led_handler;
    my_dev->urb_led->previous=(char*)malloc(sizeof(char));
    *my_dev->urb_led->previous='\0';
    my_dev->caps_mode=0;
    my_dev->pending_led_cmd=0;
    sem_init(&(my_dev->led_urb_lock),0,1);
    //sem_init(&my_dev->led_buff_lock,0,1);
    my_dev->irq_urb_submitted=true;
    my_dev->led_urb_submitted=false;
    if(sem_init(&(my_dev->submit_led),0,0)==0){
    	sem_post(&(my_dev->submit_led));
    	sem_wait(&(my_dev->submit_led));
    	
    }
    submit_urb(my_dev->urb_irq, my_dev->urb_led);
    return 0;
}

void* irq_func(){
    int fd=open("input.txt",O_RDONLY);
    char* buf=(char*)malloc(sizeof(char));
    while(read(STDIN_FILENO,buf,1)>0){
        //printf("%c\n",*buf);
        write(irq_pipe[1],buf,1);
        //printf("%c\n",*buf);
        sleep(0.5);
    }
    close(irq_pipe[1]);
    close(fd);
    //printf("closing device\n");
    return 0;
}
void* ctl_func(){
    char* caps_led_buf=(char*)malloc(sizeof(char));
    char* bufr=(char*)malloc(sizeof(char));
    char bufw='A';
    int count=1;
    while(read(ctl_pipe[0],bufr,1)>0){
        caps_led_buf=(char*)realloc(caps_led_buf,count*sizeof(char));
        if(*led_buff==0){
            caps_led_buf[count-1]='0';
        }
        else{
            caps_led_buf[count-1]='1';
        }
        write(ack_pipe[1],&bufw,1);
        count++;
    }
    close(ack_pipe[1]);
    close(ctl_pipe[0]);
    for(int i=0;i<count-1;i++){
        if(caps_led_buf[i]=='0'){
            printf("off ");
        }
        else{
            printf("on ");
        }
    }
    printf("\n");
    exit(0);
}
int main(){
    //get the setup done-->create 3 pipes and buffer for leds to which the core function writes and from which the keyboard reads
    led_buff=(int*)mmap(NULL,1,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,0,0);//led_buff[0] represents older led status 
    *led_buff=0;
    sem_init(&status_lock,0,1);
    if(pipe(irq_pipe)<0 || pipe(ack_pipe)<0 || pipe(ctl_pipe)<0){
        printf("pipe creation failed\n");
        return 0;
    }
    //create one child process---> the keyboard process
    switch(fork()){
        case -1:
            printf("process creation failed\n");
            return 0;
        case 0:
            close(irq_pipe[0]);
            close(ack_pipe[0]);
            close(ctl_pipe[1]);
            pthread_t kbd_irq_endpoint;
            pthread_t kbd_ctl_endpoint;
            if(!pthread_create(&kbd_irq_endpoint,NULL,irq_func,NULL)<0){
                printf("kbd_irq thread not created \n");
                return 0;
            }
            if(pthread_create(&kbd_ctl_endpoint,NULL,ctl_func,NULL)<0){
                printf("kbd_ctl thread not created \n");
                return 0;
            }
            pthread_join(kbd_irq_endpoint,NULL);
            pthread_join(kbd_ctl_endpoint,NULL);
            return 0;
        default:
            close(irq_pipe[1]);
            close(ack_pipe[1]);
            close(ctl_pipe[0]);
            open_my_kbd();
    }
    return 0;
}
