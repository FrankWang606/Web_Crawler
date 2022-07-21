/*

/** 
 * @file main_wirte_read_cb.c
 * @brief cURL write call back to save received data in a user defined memory first
 *        and then write the data to a file for verification purpose.
 *        cURL header call back extracts data sequence number from header if there is a sequence number.
 * @see https://curl.haxx.se/libcurl/c/getinmemory.html
 * @see https://curl.haxx.se/libcurl/using/
 * @see https://ec.haxx.se/callback-write.html
 */ 

#include <sys/wait.h>
#include <time.h>
#include <sys/time.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

//global variable
    double times[2];
    struct timeval tv;

int current_png=0;
char todo_url[6000][256];
int todo_current = 0;
int todo_size = 0;
char url[256];
int find_png = 0;
int max_png = 50;
int  thread =1;
int active_thread=0;
    int show_view = 0;
    char view_file[256];

FILE *opng;
FILE *op_work;
pthread_mutex_t lock1,lock2,lock3,lock4,lock5,lock6;
sem_t todo_sem;
int todo_semval = 1;


int is_png(unsigned char *buf);

int is_png(unsigned char *buf){
    unsigned char png[8] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a};
    for(int i=0;i<8;i++){
        if(png[i]!= buf[i]){
            return 1;
        }
    }
    return 0;
}

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;


htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);


htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        //fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        //printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        //printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        //printf("No result\n");
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
    ENTRY e;
		
    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                
                e.key = (char*)href;
                if(hsearch(e,FIND)){
                    xmlFree(href);
                    continue;
                } 
                //printf("href: %s\n", href);
                pthread_mutex_lock(&lock2);
                strcpy(todo_url[todo_size],(char*)href);
                todo_size++;
                pthread_mutex_lock(&lock4);
                sem_post(&todo_sem);
                todo_semval++;
                pthread_mutex_unlock(&lock4);
                pthread_mutex_unlock(&lock2);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    //xmlCleanupParser();
    return 0;
}
/**
 * @brief  cURL header call back function to extract image sequence number from 
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line 
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    //printf("%s", p_recv);
#endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}


/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv, 
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        //curl_global_cleanup();
        recv_buf_cleanup(ptr);
}
/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3; 
    }
    return fclose(fp);
}

/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    //char fname[256];
    int follow_relative_link = 1;
    char *url = NULL; 
    //pid_t pid =getpid();

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 
    //sprintf(fname, "./output_%d.html", pid);
    //return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{

    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ( eurl != NULL) {
        //printf("The PNG url is: %s\n", eurl);
    }
    if(is_png((unsigned char*)p_recv_buf->buf)==0){
        pthread_mutex_lock(&lock3);
        find_png++;
        opng = fopen("png_urls.txt","a");
        fprintf(opng,"%s\n",eurl);
        fclose(opng);
        if(find_png>=max_png){
            if (gettimeofday(&tv, NULL) != 0) {
            perror("gettimeofday");
            //abort();
        }
            times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
            printf("findpng2 execution time: %.6lf seconds\n",times[1] - times[0]);
            exit(0);
        }
        pthread_mutex_unlock(&lock3);
    }
    //sprintf(fname, "./output_%d_%d.png", p_recv_buf->seq, pid);
    //return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
    return 0;
}
/**
 * @brief process teh download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data. 
 * @return 0 on success; non-zero otherwise
 */

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    char fname[256];
    pid_t pid =getpid();
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
	    //printf("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) { 
    	//fprintf(stderr, "Error.\n");
        return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    	//printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    } else {
        sprintf(fname, "./output_%d", pid);
    }

    return 0;
}


//thread func
void *do_work(void *arg){
    
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    int req_now;   

    ENTRY e;
    e.data = (void*) 1; 


    int *n = arg;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    while(find_png<max_png){
        sem_wait(&todo_sem);
        //printf("who:%d  which1:%d which2:%d which3:%d\n",*n,todo_current,todo_size,todo_semval);
        pthread_mutex_lock(&lock1);
        req_now = todo_current;
        todo_current++;
        pthread_mutex_lock(&lock4);
        active_thread++;
        todo_semval--; 
        pthread_mutex_unlock(&lock4);
        pthread_mutex_unlock(&lock1);
        e.key = todo_url[req_now];

        pthread_mutex_lock(&lock6);
        if(hsearch(e,FIND)){
            pthread_mutex_lock(&lock4);
            active_thread--;
            if(todo_semval<=0 && active_thread<=0){
                if (gettimeofday(&tv, NULL) != 0) {
                    perror("gettimeofday");
                    //abort();
                }
                times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
                printf("findpng2 execution time: %.6lf seconds\n",times[1] - times[0]);
                exit(0);
            }
            pthread_mutex_unlock(&lock4);
            pthread_mutex_unlock(&lock6);
            continue;
        }else{ 
            hsearch(e,ENTER);
            pthread_mutex_unlock(&lock6);

        }
        curl_handle = easy_handle_init(&recv_buf, todo_url[req_now]);
        
        if ( curl_handle == NULL ) {
            fprintf(stderr, "Curl initialization failed. Exiting...\n");
            curl_global_cleanup();
            abort();
        }
        // get it! 
        res = curl_easy_perform(curl_handle);

        if( res != CURLE_OK) {
            //fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            cleanup(curl_handle, &recv_buf);
            if(show_view==1){
                pthread_mutex_lock(&lock5);
                op_work = fopen(view_file,"a");
                fprintf(op_work,"%s\n",todo_url[req_now]);
                fclose(op_work);
                pthread_mutex_unlock(&lock5);
            }
            pthread_mutex_lock(&lock4);
            active_thread--;
            if(todo_semval<=0 && active_thread<=0){
                if (gettimeofday(&tv, NULL) != 0) {
                    perror("gettimeofday");
                    //abort();
            }
                times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
                printf("findpng2 execution time: %.6lf seconds\n",times[1] - times[0]);
                exit(0);
            }
            pthread_mutex_unlock(&lock4);
            continue;
            //exit(1);
     } else {
	    //printf("%lu bytes received in memory %p, seq=%d.\n", 
               //recv_buf.size, recv_buf.buf, recv_buf.seq);
    }

        // process the download data 
        if(show_view==1){
                pthread_mutex_lock(&lock5);
                op_work = fopen(view_file,"a");
                fprintf(op_work,"%s\n",todo_url[req_now]);
                fclose(op_work);
                pthread_mutex_unlock(&lock5);
            }
        process_data(curl_handle, &recv_buf);

        // cleaning up 
        cleanup(curl_handle, &recv_buf);   
        pthread_mutex_lock(&lock4);
        active_thread--;
        if(todo_semval<=0 && active_thread<=0){
            if (gettimeofday(&tv, NULL) != 0) {
                perror("gettimeofday");
                //abort();
            }
            times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
            printf("findpng2 execution time: %.6lf seconds\n",times[1] - times[0]);
            exit(0);
        }
        pthread_mutex_unlock(&lock4);
    }
    
    return;
}

int main( int argc, char** argv ) 
{       
        strcpy(url, SEED_URL);
        for(int i=1;i<argc;i++){
            if(strcmp(argv[i],"-t")==0){
                thread = atoi(argv[i+1]);
            }else if(strcmp(argv[i],"-m")==0){
                max_png = atoi(argv[i+1]);
            }else if(strcmp(argv[i],"-v")==0){
                strcpy(view_file, argv[i+1]);
                show_view = 1;
            }else if(argv[i][0]=='h'){
                strcpy(url,argv[i]);
            }
        }
        if (gettimeofday(&tv, NULL) != 0) {
            perror("gettimeofday");
            //abort();
        }
        times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;
        sem_init(&todo_sem, 0, 1);
        if(show_view==1){
            FILE *op = fopen(view_file,"w");
            fclose(op);
        }
        FILE *op2 = fopen("png_urls.txt","w");
        fclose(op2);
        hcreate(5000);

        strcpy(todo_url[todo_size],url);
        todo_size++;
        int thread_in[thread];
        pthread_t *p_tids = malloc(sizeof(pthread_t) * thread);
        for (int i=0; i<thread; i++) {
            thread_in[i]=i;
            pthread_create(p_tids + i, NULL, do_work, thread_in+i);
        }
        for (int i=0; i<thread; i++) {
            pthread_join(p_tids[i], NULL);
        }

        free(p_tids);
        hdestroy();
    sem_destroy(&todo_sem);
    if (gettimeofday(&tv, NULL) != 0) {
            perror("gettimeofday");
            //abort();
        }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n",times[1] - times[0]);
    return 0; 
}
