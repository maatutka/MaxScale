#include <harness.h>

int harness_init(int argc, char** argv){
	int i = 0;  
	if(!(argc == 2 && strcmp(argv[1],"-h") == 0)){
		skygw_logmanager_init(0,NULL);
	}
 
	if(!(instance.head = calloc(1,sizeof(FILTERCHAIN))))
		{
			printf("Error: Out of memory\n");
			skygw_log_write(LOGFILE_ERROR,"Error: Out of memory\n");
      
			return 1;
		}

	instance.running = 1;
	instance.infile = -1;
	instance.outfile = -1;
	instance.buff_ind = -1;
	instance.last_ind = -1;
	instance.sess_ind = -1;

	process_opts(argc,argv);
	
	if(!(instance.thrpool = malloc(instance.thrcount * sizeof(pthread_t)))){
		printf("Error: Out of memory\n");
		skygw_log_write(LOGFILE_ERROR,"Error: Out of memory\n");
		return 1;
	}
  
	/**Initialize worker threads*/
	pthread_mutex_lock(&instance.work_mtx);
	size_t thr_num = 1;
	for(i = 0;i<instance.thrcount;i++){
		pthread_create(&instance.thrpool[i],NULL,(void*)work_buffer,(void*)thr_num++);
	}

	return 0;
}

void free_filters()
{
	int i;
	if(instance.head){
		while(instance.head->next){
			FILTERCHAIN* tmph = instance.head;

			instance.head = instance.head->next;
			if(tmph->instance){
				for(i = 0;i<instance.session_count;i++){
					if(tmph->filter && tmph->session[i]){
						tmph->instance->freeSession(tmph->filter,tmph->session[i]);
					}
				}
			}
			free(tmph->filter);
			free(tmph->session);
			free(tmph->down);
			free(tmph->name);
			free(tmph);
		}
	}
}

void free_buffers()
{
	if(instance.buffer){
		int i;
		for(i = 0;i<instance.buffer_count;i++){
			gwbuf_free(instance.buffer[i]);	  
		}
		free(instance.buffer);
		instance.buffer = NULL;
		instance.buffer_count = 0;

	}
  
	if(instance.infile >= 0){
		close(instance.infile);
		free(instance.infile_name);
		instance.infile = -1;
	}
}
int open_file(char* str, unsigned int write)
{
	int mode;

	if(write){
		mode = O_RDWR|O_CREAT;
	}else{
		mode = O_RDONLY;
	}
  
	return open(str,mode,S_IRWXU|S_IRGRP|S_IXGRP|S_IXOTH);
}


FILTER_PARAMETER** read_params(int* paramc)
{
	char buffer[256];
	char* token;
	char* names[64];
	char* values[64];
	int pc = 0, do_read = 1, val_len = 0;
	int i;

	memset(names,0,64);
	memset(values,0,64);
	printf("Enter filter parametes as <name>=<value>, enter \"done\" to stop.\n");
	while(do_read){

		memset(buffer,0,256);
		printf(">");
		fgets(buffer,255,stdin);
		if(strcmp("done\n",buffer) == 0){
			do_read = 0;
		}else{
			token = strtok(buffer,"=\n");
			if(token!=NULL){
				val_len = strcspn(token," \n\0");
				if((names[pc] = calloc((val_len + 1),sizeof(char))) != NULL){
					memcpy(names[pc],token,val_len);
				}
			}
			token = strtok(NULL,"=\n");
			if(token!=NULL){
				val_len = strcspn(token," \n\0");
				if((values[pc] = calloc((val_len + 1),sizeof(char))) != NULL){
					memcpy(values[pc],token,val_len);
				}
				pc++;
			}
      
		}
		if(pc >= 64){
			do_read = 0;
		}
	}
	FILTER_PARAMETER** params;
	if((params = malloc(sizeof(FILTER_PARAMETER*)*(pc+1)))!=NULL){
		for(i = 0;i<pc;i++){
			params[i] = malloc(sizeof(FILTER_PARAMETER));
			if(params[i]){
				params[i]->name = strdup(names[i]);
				params[i]->value = strdup(values[i]);
			}
			free(names[i]);
			free(values[i]);
		}
	}
	params[pc] = NULL;
	*paramc = pc;
	return params;
}

int routeQuery(void* ins, void* session, GWBUF* queue)
{

	unsigned int buffsz = 0;
	char *qstr;

	buffsz = (char*)queue->end - ((char*)queue->start + 5);

	if(queue->hint){
		buffsz += 40;
		if(queue->hint->data){
			buffsz += strnlen(queue->hint->data,1024);
		}
		if(queue->hint->value){
			buffsz += strnlen(queue->hint->value,1024);
		}
	}
	
	qstr = calloc(buffsz + 1,sizeof(char));

	if(qstr){
		memcpy(qstr,queue->start + 5,buffsz);
		if(queue->hint){
			char *ptr = qstr + strlen(qstr);

			switch(queue->hint->type){
			case HINT_ROUTE_TO_MASTER:
				sprintf(ptr,"|HINT_ROUTE_TO_MASTER");
				break;

			case HINT_ROUTE_TO_SLAVE:
				sprintf(ptr,"|HINT_ROUTE_TO_SLAVE");
				break;

			case HINT_ROUTE_TO_NAMED_SERVER:
				sprintf(ptr,"|HINT_ROUTE_TO_NAMED_SERVER");
				break;

			case HINT_ROUTE_TO_UPTODATE_SERVER:
				sprintf(ptr,"|HINT_ROUTE_TO_UPTODATE_SERVER");
				break;

			case HINT_ROUTE_TO_ALL:
				sprintf(ptr,"|HINT_ROUTE_TO_ALL");
				break;
	
			case HINT_PARAMETER:
				sprintf(ptr,"|HINT_PARAMETER");
				break;

			default:
				sprintf(ptr,"|HINT_UNDEFINED");
				break;

			}

			ptr = qstr + strlen(qstr);
			if(queue->hint->data){
				sprintf(ptr,"|%s",(char*)queue->hint->data);
				ptr = qstr + strlen(qstr);
			}
			if(queue->hint->value){
				sprintf(ptr,"|%s",(char*)queue->hint->value);
				ptr = qstr + strlen(qstr);
			}
		}

	}else{
		printf("Error: cannot allocate enough memory.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
		return 0;
	}

	if(instance.verbose){
		printf("Query endpoint: %s\n", qstr);    
	}
  
	if(instance.outfile>=0){
		write(instance.outfile,qstr,strlen(qstr));
		write(instance.outfile,"\n",1);
	}

	free(qstr);
	return 1;
}


int clientReply(void* ins, void* session, GWBUF* queue)
{
  
	if(instance.verbose){
		pthread_mutex_lock(&instance.work_mtx);
		unsigned char* ptr = (unsigned char*)queue->start;
		unsigned int i,pktsize = 4 + ptr[0] + (ptr[1] << 8) + (ptr[2] << 16);
		printf("Reply endpoint: ");
		for(i = 0;i<pktsize;i++){
			printf("%.2x ",*ptr++);
		}
		printf("\n");
		pthread_mutex_unlock(&instance.work_mtx);
	}
  
	if(instance.outfile>=0){
		int qlen = queue->end - queue->start;
		write(instance.outfile,"Reply: ",strlen("Reply: "));
		write(instance.outfile,queue->start,qlen);
		write(instance.outfile,"\n",1);
    
	}

	return 1;
}


int load_query()
{
	char** query_list;
	char* buff;
	char rc;
	int i, qcount = 0, qbuff_sz = 10, buff_sz = 2048;
	int offset = 0;
	unsigned int qlen = 0;
  
	if((buff = calloc(buff_sz,sizeof(char))) == NULL || 
	   (query_list = calloc(qbuff_sz,sizeof(char*))) == NULL){
		printf("Error: cannot allocate enough memory.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
		return 1;
	}


	while(read(instance.infile,&rc,1)){

		if(rc != '\n' && rc != '\0'){
      
			if(offset >= buff_sz){
				char* tmp = malloc(sizeof(char)*2*buff_sz);

				if(tmp){
					memcpy(tmp,buff,buff_sz);
					free(buff);
					buff = tmp;
					buff_sz *= 2;
				}else{
					printf("Error: cannot allocate enough memory.\n");
					skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
					free(buff);
					return 1;
				}
			}

			buff[offset++] = rc;
     
		}else{


			if(qcount >= qbuff_sz){
				char** tmpcl = malloc(sizeof(char*) * (qcount * 2 + 1));
				if(!tmpcl){
					printf("Error: cannot allocate enough memory.\n");
					skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
					return 1;
				}
				for(i = 0;i < qbuff_sz;i++){
					tmpcl[i] = query_list[i];
				}
				free(query_list);
				query_list = tmpcl;
				qbuff_sz = qcount * 2 + 1;
			}
      
			query_list[qcount] = malloc(sizeof(char)*(offset + 1));
			memcpy(query_list[qcount],buff,offset);
			query_list[qcount][offset] = '\0';
			offset = 0;
			qcount++;

		}

	}

	GWBUF** tmpbff = malloc(sizeof(GWBUF*)*(qcount + 1));
	if(tmpbff){
		for(i = 0;i<qcount;i++){
    
			tmpbff[i] = gwbuf_alloc(strnlen(query_list[i],buff_sz) + 6);
			gwbuf_set_type(tmpbff[i],GWBUF_TYPE_MYSQL);
			memcpy(tmpbff[i]->sbuf->data + 5,query_list[i],strnlen(query_list[i],buff_sz));
      
			qlen = strnlen(query_list[i],buff_sz);
			tmpbff[i]->sbuf->data[0] = qlen;
			tmpbff[i]->sbuf->data[1] = (qlen << 8);
			tmpbff[i]->sbuf->data[2] = (qlen << 16);
			tmpbff[i]->sbuf->data[3] = 0x00;
			tmpbff[i]->sbuf->data[4] = 0x03;

		}
		tmpbff[qcount] = NULL;
		instance.buffer = tmpbff;
	}else{
		printf("Error: cannot allocate enough memory for buffers.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory for buffers.\n");    
		free_buffers();
		return 1;
	}
	if(qcount < 1){
		return 1;
	}
  
	instance.buffer_count = qcount;
	return 0;
}


int handler(void* user, const char* section, const char* name,
			const char* value)
{

	CONFIG* conf = instance.conf;
	if(conf == NULL){/**No sections handled*/

		if((conf = malloc(sizeof(CONFIG))) &&
		   (conf->item = malloc(sizeof(CONFIG_ITEM)))){

			conf->section = strdup(section);
			conf->item->name = strdup(name);
			conf->item->value = strdup(value);
			conf->item->next = NULL;
			conf->next = NULL;

		}

	}else{

		CONFIG* iter = instance.conf;

		/**Finds the matching section*/
		while(iter){
			if(strcmp(iter->section,section) == 0){
				CONFIG_ITEM* item = malloc(sizeof(CONFIG_ITEM));
				if(item){
					item->name = strdup(name);
					item->value = strdup(value);
					item->next = iter->item;
					iter->item = item;
					break;
				}
			}else{
				iter = iter->next;
			}
		}

		/**Section not found, creating a new one*/
		if(iter == NULL){
      
			CONFIG* nxt = malloc(sizeof(CONFIG));
			if(nxt && (nxt->item = malloc(sizeof(CONFIG_ITEM)))){
				nxt->section = strdup(section);
				nxt->item->name = strdup(name);
				nxt->item->value = strdup(value);
				nxt->item->next = NULL;
				nxt->next = conf;
				conf = nxt;

			}

		}

	}

	instance.conf = conf;
	return 1;
}


CONFIG* process_config(CONFIG* conf)
{
	CONFIG* tmp;
	CONFIG* tail = conf;
	CONFIG* head = NULL;
	CONFIG_ITEM* item;

	while(tail){
		item = tail->item;

		while(item){

			if(strcmp("type",item->name) == 0 &&
			   strcmp("filter",item->value) == 0){
				tmp = tail->next;
				tail->next = head;
				head = tail;
				tail = tmp;
				break;
			}else{
				item = item->next;
			}
		}

		if(item == NULL){
			tail = tail->next;
		}

	}

	return head;
}



int load_config( char* fname)
{
	CONFIG* iter;
	CONFIG_ITEM* item;
	int config_ok = 1;
	free_filters();
	if(ini_parse(fname,handler,instance.conf) < 0){
		printf("Error parsing configuration file!\n");
		skygw_log_write(LOGFILE_ERROR,"Error parsing configuration file!\n");
		config_ok = 0;
		goto cleanup;
	}
	if(instance.verbose){
		printf("Configuration loaded from %s\n\n",fname);
	}
	if(instance.conf == NULL){
		printf("Nothing valid was read from the file.\n");
		skygw_log_write(LOGFILE_MESSAGE,"Nothing valid was read from the file.\n");
		config_ok = 0;
		goto cleanup;
	}

	instance.conf = process_config(instance.conf);
	if(instance.conf){
		if(instance.verbose){
			printf("Modules Loaded:\n");
		}
		iter = instance.conf;
	}else{
		printf("No filters found in the configuration file.\n");
		skygw_log_write(LOGFILE_MESSAGE,"No filters found in the configuration file.\n");
		config_ok = 0;
		goto cleanup;
	}

	while(iter){
		item = iter->item;
		while(item){
      
			if(!strcmp("module",item->name)){

				if(instance.mod_dir){
					char* modstr = malloc(sizeof(char)*(strlen(instance.mod_dir) + strlen(item->value) + 1));
					strcpy(modstr,instance.mod_dir);
					strcat(modstr,"/");
					strcat(modstr,item->value);
					instance.head = load_filter_module(modstr);
					free(modstr);
				}else{
					instance.head = load_filter_module(item->value);
				}


				if(!instance.head || !load_filter(instance.head,instance.conf)){

					printf("Error creating filter instance!\nModule: %s\n",item->value);
					skygw_log_write(LOGFILE_ERROR,"Error creating filter instance!\nModule: %s\n",item->value);
					config_ok = 0;
					goto cleanup;

				}else{
					if(instance.verbose){
						printf("\t%s\n",iter->section);  
					}
				}
			}
			item = item->next;
		}
		iter = iter->next;
	}

	while(instance.conf){
		item = instance.conf->item;
		while(item){
			item = instance.conf->item;
			instance.conf->item = instance.conf->item->next;
			free(item->name);
			free(item->value);
			free(item);
			item = instance.conf->item;
		}
		instance.conf = instance.conf->next;
    
	}

	cleanup:
	while(instance.conf){
		iter = instance.conf;
		instance.conf = instance.conf->next;
		item = iter->item;

		while(item){      
			free(item->name);
			free(item->value);
			free(item);
			iter->item = iter->item->next;
			item = iter->item;
		}

		free(iter);
	}
	instance.conf = NULL;

	return config_ok;
}

int load_filter(FILTERCHAIN* fc, CONFIG* cnf)
{
	FILTER_PARAMETER** fparams;
	int i, paramc = -1;
	if(cnf == NULL){
   
		fparams = read_params(&paramc);
 
	}else{

		CONFIG* iter = cnf;
		CONFIG_ITEM* item;
		while(iter){
			paramc = -1;
			item = iter->item;
      
			while(item){

				/**Matching configuration found*/
				if(!strcmp(item->name,"module") && !strcmp(item->value,fc->name)){
					paramc = 0;
					item = iter->item;
	  
					while(item){
						if(strcmp(item->name,"module") && strcmp(item->name,"type")){
							paramc++;
						}
						item = item->next;
					}
					item = iter->item;
					fparams = calloc((paramc + 1),sizeof(FILTER_PARAMETER*));
					if(fparams){
	    
						int i = 0;
						while(item){
							if(strcmp(item->name,"module") != 0 &&
							   strcmp(item->name,"type") != 0){
								fparams[i] = malloc(sizeof(FILTER_PARAMETER));
								if(fparams[i]){
									fparams[i]->name = strdup(item->name);
									fparams[i]->value = strdup(item->value);
									i++;
								}
							}
							item = item->next;
						}

					}

				}

				if(paramc > -1){
					break;
				}else{
					item = item->next;
				}

			}

			if(paramc > -1){
				break;
			}else{
				iter = iter->next;
			}

		}
	}

	int sess_err = 0;

	if(fc && fc->instance){


		fc->filter = (FILTER*)fc->instance->createInstance(NULL,fparams);
    
		for(i = 0;i<instance.session_count;i++){

			if((fc->session[i] = fc->instance->newSession(fc->filter, fc->session[i])) &&
			   (fc->down[i] = calloc(1,sizeof(DOWNSTREAM))) &&
			   (fc->up[i] = calloc(1,sizeof(UPSTREAM)))){

				fc->up[i]->session = NULL;
				fc->up[i]->instance = NULL;
				fc->up[i]->clientReply = (void*)clientReply;

				if(fc->instance->setUpstream && fc->instance->clientReply){
					fc->instance->setUpstream(fc->filter, fc->session[i], fc->up[i]);
				}else{
					skygw_log_write(LOGFILE_MESSAGE,
									"Warning: The filter %s does not support client relies.\n",fc->name);
				}

				if(fc->next && fc->next->next){ 

					fc->down[i]->routeQuery = (void*)fc->next->instance->routeQuery;
					fc->down[i]->session = fc->next->session[i];
					fc->down[i]->instance = fc->next->filter;
					fc->instance->setDownstream(fc->filter, fc->session[i], fc->down[i]);

					fc->next->up[i]->clientReply = (void*)fc->instance->clientReply;
					fc->next->up[i]->session = fc->session[i];
					fc->next->up[i]->instance = fc->filter;

					if(fc->instance->setUpstream && fc->instance->clientReply){
						fc->next->instance->setUpstream(fc->next->filter,fc->next->session[i],fc->next->up[i]);
					}

				}else{ /**The dummy router is the next one*/

					fc->down[i]->routeQuery = (void*)routeQuery;
					fc->down[i]->session = NULL;
					fc->down[i]->instance = NULL;
					fc->instance->setDownstream(fc->filter, fc->session[i], fc->down[i]);

				}

			}


			if(!fc->session[i] || !fc->down[i] || !fc->up[i]){

				sess_err = 1;
				break;

			}

		}
    
		if(sess_err){
			for(i = 0;i<instance.session_count;i++){
				if(fc->filter && fc->session[i]){
					fc->instance->freeSession(fc->filter, fc->session[i]);
				}
				free(fc->down[i]);
			}
			free(fc->session);
			free(fc->down);
			free(fc->name);
			free(fc);
		}
    
	}
  
	if(cnf){
		int x;
		for(x = 0;x<paramc;x++){
			free(fparams[x]->name);
			free(fparams[x]->value);
		}
		free(fparams);
	}

	return sess_err ? 0 : 1;
}


FILTERCHAIN* load_filter_module(char* str)
{
	FILTERCHAIN* flt_ptr = NULL;
	if((flt_ptr = calloc(1,sizeof(FILTERCHAIN))) != NULL && 
	   (flt_ptr->session = calloc(instance.session_count,sizeof(SESSION*))) != NULL &&
	   (flt_ptr->down = calloc(instance.session_count,sizeof(DOWNSTREAM*))) != NULL && 
	   (flt_ptr->up = calloc(instance.session_count,sizeof(UPSTREAM*))) != NULL){
		flt_ptr->next = instance.head;
	}

	if((flt_ptr->instance = (FILTER_OBJECT*)load_module(str, MODULE_FILTER)) == NULL)
		{
			printf("Error: Module loading failed: %s\n",str);
			skygw_log_write(LOGFILE_ERROR,"Error: Module loading failed: %s\n",str);
			free(flt_ptr->down);
			free(flt_ptr);
			return NULL;
		}
	flt_ptr->name = strdup(str);
	return flt_ptr;
}


void route_buffers()
{
	if(instance.buffer_count > 0){
		float tprg = 0.f, bprg = 0.f, trig = 0.f,
			fin = instance.buffer_count*instance.session_count,
			step = (fin/50.f)/fin;
		FILTERCHAIN* fc = instance.head;
    
		while(fc->next->next){
			fc = fc->next;
		}
		instance.tail = fc;

		instance.buff_ind = 0;
		instance.sess_ind = 0;
		instance.last_ind = 0;

		printf("Routing queries...\n");

		if(!instance.verbose){
			printf("%s","|0%");
			float f;
			for(f = 0.f;f<1.f - step*7;f += step){
				printf(" ");
			}
			printf("%s\n","100%|");
			write(1,"|",1);
		}

		while(instance.buff_ind < instance.buffer_count){
			pthread_mutex_unlock(&instance.work_mtx);
			while(instance.last_ind < instance.session_count){
	
				tprg = ((bprg + (float)instance.last_ind)/fin);
				if(!instance.verbose){
					if(tprg >= trig){
						write(1,"-",1);
						trig += step;
					}
				}
				usleep(100);
			}
			pthread_mutex_lock(&instance.work_mtx);
			instance.buff_ind++;
			bprg += instance.last_ind;
			instance.sess_ind = 0;
			instance.last_ind = 0;

      

		}
		if(!instance.verbose){
			write(1,"|\n",2);
		}
		printf("Queries routed.\n");
	}

}


void work_buffer(void* thr_num)
{
	unsigned int index = instance.session_count;
	GWBUF* fake_ok = gen_packet(PACKET_OK);
	while(instance.running){

		pthread_mutex_lock(&instance.work_mtx);
		pthread_mutex_unlock(&instance.work_mtx);

		index = atomic_add(&instance.sess_ind,1);

		if(instance.running &&
		   index < instance.session_count &&
		   instance.buff_ind < instance.buffer_count)
			{
				instance.head->instance->routeQuery(instance.head->filter,
													instance.head->session[index],
													instance.buffer[instance.buff_ind]);
				if(instance.tail->instance->clientReply){
					instance.tail->instance->clientReply(instance.tail->filter,
														 instance.tail->session[index],
														 fake_ok);
				}
				atomic_add(&instance.last_ind,1);
				usleep(1000*instance.rt_delay);
			}

	}
	gwbuf_free(fake_ok);
}


GWBUF* gen_packet(PACKET pkt)
{
	unsigned int psize = 0;
	GWBUF* buff = NULL;
	unsigned char* ptr;
	switch(pkt){
	case PACKET_OK:
		psize = 11;
		break;

	default:
		break;

	}
	if(psize > 0){
		buff = gwbuf_alloc(psize);
		ptr = (unsigned char*)buff->start;
  
		switch(pkt){
		case PACKET_OK:

			ptr[0] = 7; /**Packet size*/
			ptr[1] = 0;
			ptr[2] = 0;

			ptr[3] = 1; /**sequence_id*/

			ptr[4] = 0; /**OK header*/

			ptr[5] = 0; /**affected_rows*/

			ptr[6] = 0; /**last_insert_id*/

			ptr[7] = 0; /**status_flags*/
			ptr[8] = 0;

			ptr[9] = 0; /**warnings*/
			ptr[10] = 0;
			break;

		default:
			break;

		}
	}
	return buff;
}


int process_opts(int argc, char** argv)
{
	int fd = open_file("harness.cnf",1), buffsize = 1024;
	int rd,fsize;
	char *buff = calloc(buffsize,sizeof(char)), *tok = NULL;

	/**Parse 'harness.cnf' file*/
	fsize = lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
	instance.thrcount = 1;
	instance.session_count = 1;
	read(fd,buff,fsize);
	tok = strtok(buff,"=");
	while(tok){
		if(!strcmp(tok,"threads")){
			tok = strtok(NULL,"\n\0");
			instance.thrcount = strtol(tok,0,0);
		}else if(!strcmp(tok,"sessions")){
			tok = strtok(NULL,"\n\0");
			instance.session_count = strtol(tok,0,0);
		}
		tok = strtok(NULL,"=");
	}
  
  
   
	free(buff);
	instance.verbose = 1;

	if(argc < 2){
		return 1;
	}
	char* conf_name = NULL;
	while((rd = getopt(argc,argv,"m:c:i:o:s:t:d:qh")) > 0){
		switch(rd){

		case 'o':
			instance.outfile = open_file(optarg,1);
			printf("Output is written to: %s\n",optarg);
			break;

		case 'i':
			instance.infile = open_file(optarg,0);
			printf("Input is read from: %s\n",optarg);
			break;

		case 'c':
			conf_name = strdup(optarg);
			break;

		case 'q':
			instance.verbose = 0;
			break;

		case 's':
			instance.session_count = atoi(optarg);
			printf("Sessions: %i ",instance.session_count);
			break;

		case 't':
			instance.thrcount = atoi(optarg);
			printf("Threads: %i ",instance.thrcount);
			break;

		case 'd':
			instance.rt_delay = atoi(optarg);
			printf("Routing delay: %i ",instance.rt_delay);
			break;

		case 'h':
			printf(
				   "\nOptions for the configuration file 'harness.cnf'':\n\n"
				   "\tthreads\tNumber of threads to use when routing buffers\n"
				   "\tsessions\tNumber of sessions\n\n"
				   "Options for the command line:\n\n"
				   "\t-h\tDisplay this information\n"
				   "\t-c\tPath to the MaxScale configuration file to parse for filters\n"
				   "\t-i\tName of the input file for buffers\n"
				   "\t-o\tName of the output file for results\n"
				   "\t-q\tSuppress printing to stdout\n"
				   "\t-s\tNumber of sessions\n"
				   "\t-t\tNumber of threads\n"
				   "\t-d\tRouting delay\n");
			break;

		case 'm':
			instance.mod_dir = strdup(optarg);
			printf("Module directory: %s",optarg);
			break;

		default:
	
			break;

		}
	}
	printf("\n");
	if(conf_name && load_config(conf_name)){
		load_query();
	}else{
		instance.running = 0;
	}

	return 0;
}