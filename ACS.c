#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
struct customer_info{ /// use this struct to record the customer information read from customers.txt
    int user_id;
	int class_type;
	int service_time;
	int arrival_time;
};
struct QNode {
    struct customer_info *customer;
    struct QNode* next;
};
// The queue, front stores the front node of LL and rear stores the
// last node of LL
struct Queue {
    struct QNode *front, *rear;
	int length;
};
struct customer_info *customer_generator(char *line);
struct Queue* createQueue(); //creates an empty queue
struct QNode* newNode(struct customer_info *customer); //creates a qnode that points to input customer
void enQueue(struct Queue* q, struct customer_info *customer); //adds a customer to the queue (calls newNode internally)
struct customer_info * deQueue(struct Queue* q); //removes from the front of the queue, returns the customer pointed to by that node
void *clerk_entry(void * clerkNum); // entry function for clerk threads
void * customer_entry(void *); // entry function for customer threads
/* global variables */
struct Queue *business_q;
struct Queue *economy_q;
struct timeval init_time; // use this variable to record the simulation start time; No need to use mutex_lock when reading this variable since the value would not be changed by thread once the initial time was set.
int available_clerks;
int clerks[5];
int total_business_class; // number of business class customers
int total_econ_class; //number of econ class customers
double total_wait_time; //total wait time of all customers
double business_wait_time; //total wait time of all business class
double economy_wait_time;  //total wait time of all econ class
pthread_mutex_t total_wait_lock; // mutex for modifying these
pthread_cond_t noclerks; // forces customer thread to wait while there are no clerks
/* Other global variable may include:
 1. condition_variables (and the corresponding mutex_lock) to represent each queue;
 2. condition_variables to represent clerks
 3. others.. depend on your design
 */
pthread_mutex_t customer_enqueue_lock; //mutex for customers adding themselves to the queue
pthread_mutex_t customer_clerk_communication; //mutex for customers checking on clerks
int main(int argc, char *argv[]) {
	available_clerks = 5;
	total_econ_class = 0;
	total_business_class = 0;
	total_wait_time = 0;
	business_wait_time = 0;
	economy_wait_time = 0;
	for(int i = 0; i < 5; i++) {
		clerks[i] = 0;
	}
	int rc = gettimeofday(&init_time, NULL);
	if(rc) {
		printf("Issue getting init time. error code %d\n", rc);
		exit(-1);
	}
	business_q = createQueue();
	economy_q = createQueue();
	// initialize all the condition variable and thread lock will be used

	/** Read customer information from txt file and store them in the structure you created

		1. Allocate memory(array, link list etc.) to store the customer information.
		2. File operation: fopen fread getline/gets/fread ..., store information in data structure you created

	*/

	char line[50];
    char *target_path = argv[1];
    FILE *f = fopen(target_path, "r");
	if(f == NULL) {
		fprintf(stdout, "Error openining file\n");
		exit(-1);
	}
	int i = 0;
	int max_customers = 1;
	struct customer_info ** customers = malloc(9999 * sizeof(struct customer_info));
    while(fgets(line, 50, f) != NULL) {
		if(i == 0) {
			line[strcspn(line, "\n")] = 0;
			max_customers = atoi(line); //get max customers from first line
		}
		else {
			customers[i-1] = customer_generator(line); //store customer_info structs in an array
		}
		i++;
    }
	fclose(f);
	//create customer thread
	int NCustomers = max_customers;
	pthread_t customer_threads[max_customers];
	for(i = 0; i < NCustomers; i++){ // number of customers
		rc = pthread_create(&customer_threads[i], NULL, customer_entry, (void *)customers[i]); //custom_info: passing the customer information (e.g., customer ID, arrival time, service time, etc.) to customer thread
		if(rc) {
			printf("Error:unable to create thread, %d\n", rc);
         	exit(-1);
		}
	}
	// wait for all customer threads to terminate
	for (int i = 0; i < max_customers; i++) {
		pthread_join(customer_threads[i], NULL);
	}
	// destroy mutex & condition variable (optional)
	printf("All jobs done...\n\n");
	// calculate the average waiting time of all customers
	double total_avg = total_wait_time/((double)max_customers);
	printf("The average waiting time for all customers in the system is: %.2f seconds. \n", total_avg);
	double business_avg = business_wait_time/((double)total_business_class);
    if(total_business_class == 0) {
        business_avg = 0;
    }
	printf("The average waiting time for all business-class customers in the system is: %.2f seconds. \n", business_avg);
	double econ_avg = economy_wait_time/((double)total_econ_class);
    if(total_econ_class == 0) {
        econ_avg = 0;
    }
	printf("The average waiting time for all economy-class customers in the system is: %.2f seconds. \n", econ_avg);
	return 0;
}

// function entry for customer threads

void * customer_entry(void * cus_info){

	struct customer_info * p_myInfo = (struct customer_info *) cus_info;

	usleep(p_myInfo->arrival_time * 10000); //sleep until customer arrives

	fprintf(stdout, "A customer arrives: customer ID %2d. \n", p_myInfo->user_id);

	/* Enqueue operation: get into either business queue or economy queue by using p_myInfo->class_type*/
	pthread_mutex_lock(&customer_enqueue_lock);
	if(p_myInfo->class_type == 1) {
		enQueue(business_q, p_myInfo);
		total_business_class++;
		fprintf(stdout, "A customer enters a queue: the queue ID %1d, and length of the queue %2d. \n", p_myInfo->class_type, business_q->length);
	}
	else {
		enQueue(economy_q, p_myInfo);
		total_econ_class++;
		fprintf(stdout, "A customer enters a queue: the queue ID %1d, and length of the queue %2d. \n", p_myInfo->class_type, economy_q->length);
	}
	pthread_mutex_unlock(&customer_enqueue_lock);

	struct timeval queue_enter_time;
	int ev = gettimeofday(&queue_enter_time, NULL);
	if(ev) {printf("error with gettimeofday, code: %d\n", ev); exit(-1);}
	double relative_queue_enter_time = queue_enter_time.tv_sec - init_time.tv_sec;
	relative_queue_enter_time += (queue_enter_time.tv_usec - init_time.tv_usec)/1000000;
	int myClerk;
	while (1) { //customer idle loop
		pthread_mutex_lock(&customer_clerk_communication);
		if(available_clerks == 0) {
			pthread_cond_wait(&noclerks, &customer_clerk_communication);
		}
		if(business_q->length > 0) {
			if (business_q->front->customer->user_id == p_myInfo->user_id && available_clerks > 0) { //check if we are the front of the queue and if a clerk is available
				deQueue(business_q);
				available_clerks--; //take one clerk from the pool
				for(int i = 0; i < 5; i++) { //check which clerk was free
					if(clerks[i] == 0) {
						myClerk = i; //set that clerk as my clerk
						clerks[i] = 1; //set it as busy
						break; //break out of the clerk search for loop
					}
				}
				pthread_mutex_unlock(&customer_clerk_communication); //done communicating with clerks, unlock
				break; //break out of idle loop
			}
		}
		if(economy_q->length > 0 ) {
			if(economy_q->front->customer->user_id == p_myInfo->user_id && available_clerks > 0 && business_q->front == NULL) { //check if we are the front of the queue, that a clerk is avaible and that business queue is empty
				deQueue(economy_q);
				available_clerks--; //take one clerk from the pool
				for(int i = 0; i < 5; i++) { //check which clerk was free
					if(clerks[i] == 0) {
						myClerk = i; //set that clerk as my clerk
						clerks[i] = 1; //set it as busy
						break; //break out of the clerk search for loop
					}
				}
				pthread_mutex_unlock(&customer_clerk_communication);
				break;
			}
		}
			pthread_mutex_unlock(&customer_clerk_communication);
		}
	/* Try to figure out which clerk awoken me, because you need to print the clerk Id information */
	//usleep(10); // Add a usleep here to make sure that all the other waiting threads have already got back to call pthread_cond_wait. 10 us will not harm your simulation time.

	/* get the current machine time; updates the overall_waiting_time*/
	struct timeval start_service_time;
	ev = gettimeofday(&start_service_time, NULL);
	if(ev) {printf("error with gettimeofday, code: %d\n", ev); exit(-1);} //catch errors from gettimeofday
	double relative_start_time;
	relative_start_time = start_service_time.tv_sec - init_time.tv_sec;
	relative_start_time += (double)(start_service_time.tv_usec - init_time.tv_usec)/1000000;
	fprintf(stdout, "A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", relative_start_time, p_myInfo->user_id, myClerk);

	usleep(p_myInfo->service_time * 100000);

	/* get the current machine time; */
	struct timeval finish_service_time;
	ev = gettimeofday(&finish_service_time, NULL);
	if(ev) {printf("error with gettimeofday, code: %d\n", ev); exit(-1);}
	double relative_end_time;
	relative_end_time = finish_service_time.tv_sec - init_time.tv_sec;
	relative_end_time += (double)(finish_service_time.tv_usec - init_time.tv_usec)/1000000;
	fprintf(stdout, "-->>> A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n", relative_end_time, p_myInfo->user_id, myClerk);\

	pthread_mutex_lock(&customer_clerk_communication);
	clerks[myClerk] = 0; //show that clerk is free to server another
	available_clerks++;
	pthread_cond_broadcast(&noclerks);
	pthread_mutex_unlock(&customer_clerk_communication);

	double waiting_time = relative_start_time - relative_queue_enter_time;
	pthread_mutex_lock(&total_wait_lock);
	total_wait_time += waiting_time;
	if(p_myInfo->class_type == 1) {
		business_wait_time += waiting_time;
	}
	else {
		economy_wait_time += waiting_time;
	}
	pthread_mutex_unlock(&total_wait_lock);
	return NULL;
}


//create a node that points to the input customer
struct QNode* newNode(struct customer_info *customer)
{
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->customer = customer;
    temp->next = NULL;
    return temp;
}

//create empty queue
struct Queue* createQueue()
{
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
	q->length = 0;
    return q;
}

// The function to add a customer to q
void enQueue(struct Queue* q, struct customer_info *customer)
{
    // Create a new LL node
    struct QNode* temp = newNode(customer);

    // If queue is empty, then new node is front and rear both
    if (q->rear == NULL) {
        q->front = q->rear = temp;
		q->length++;
        return;
    }

    // Add the new node at the end of queue and change rear
    q->rear->next = temp;
    q->rear = temp;
	q->length++;
}

// Function to remove a key from given queue q
struct customer_info * deQueue(struct Queue* q)
{
    // If queue is empty, return NULL.
    if (q->front == NULL) {
        return NULL;
	}
    // Store previous front and move front one node ahead
    struct QNode* temp = q->front;

    q->front = q->front->next;
    // If front becomes NULL, then change rear also as NULL
    if (q->front == NULL) {
        q->rear = NULL;
	}
	struct customer_info *retval = temp->customer;
    free(temp);
	q->length--;
	return retval;
}

struct customer_info *customer_generator(char *line) {
	struct customer_info *customer = (struct customer_info *)malloc(sizeof(struct customer_info));
	int i = 0;
	if(customer) {
		const char s[2] = ",";
		char *token;
		token = strtok(line, s);
		while(token != NULL) {
			if(i == 0) {
				customer->user_id = atoi(&token[0]);
				customer->class_type = atoi(&token[2]);
			}
			else if (i == 1) {
				customer->arrival_time = atoi(token);
			}
			else {
				customer->service_time = atoi(token);
			}
			i++;
			token = strtok(NULL, s);
		}
	}
	return customer;
}
