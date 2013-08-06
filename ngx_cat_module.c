

/*
 * Copyright (C) peng hu
 * Copyright (C) DianPing, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define CATPREFIX "X-CAT-"
#define SENDOUTBUFSIZE 400

int pipefd[2]; 

typedef struct {
	ngx_flag_t          enable;
} ngx_http_cat_t;

unsigned long start_upstream_sec = 0;
unsigned long start_upstream_msec = 0;
unsigned int response_start_msec = 0;
unsigned int send_times = 0;

static ngx_int_t ngx_cat_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_cat_body_filter(ngx_http_request_t *r, ngx_chain_t *chain);
static ngx_int_t ngx_cat_header_filter(ngx_http_request_t *r);
static void * ngx_cat_create_conf(ngx_conf_t *cf);
static char * ngx_cat_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static char * ngx_http_cat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t  ngx_cat_filter_commands[] = {

	{ ngx_string("Cat"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
			|NGX_CONF_TAKE1,
		ngx_http_cat,
		NGX_HTTP_LOC_CONF_OFFSET,
		0,
		NULL },

	ngx_null_command
};


static ngx_http_module_t  ngx_cat_filter_module_ctx = {
	NULL,           /* preconfiguration */
	ngx_cat_filter_init,             /* postconfiguration */

	NULL,                                  /* create main configuration */
	NULL,                                  /* init main configuration */

	NULL,                                  /* create server configuration */
	NULL,                                  /* merge server configuration */

	ngx_cat_create_conf,             /* create location configuration */
	ngx_cat_merge_conf               /* merge location configuration */
};


ngx_module_t  ngx_cat_filter_module = {
	NGX_MODULE_V1,
	&ngx_cat_filter_module_ctx,      /* module context */
	ngx_cat_filter_commands,         /* module directives */
	NGX_HTTP_MODULE,                       /* module type */
	NULL,                                  /* init master */
	NULL,                                  /* init module */
	NULL,                                  /* init process */
	NULL,                                  /* init thread */
	NULL,                                  /* exit thread */
	NULL,                                  /* exit process */
	NULL,                                  /* exit master */
	NGX_MODULE_V1_PADDING
};


static ngx_http_output_body_filter_pt  ngx_http_next_body_filter;
static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;

	static ngx_int_t
ngx_cat_filter_init(ngx_conf_t *cf)
{
	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_cat_body_filter;

	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_cat_header_filter;

	return NGX_OK;
}

int put_string_to_buf(char* buf, char* key, char* value, int *pos){
	ngx_memcpy(buf + *pos, key, strlen(key));
	*pos = *pos + strlen(key);
	ngx_memcpy(buf + *pos, value, strlen(value));
	*pos = *pos + strlen(value);
	return 0;
}

static ngx_int_t ngx_cat_header_filter(ngx_http_request_t *r){
	ngx_http_cat_t  *conf;
	conf = ngx_http_get_module_loc_conf(r, ngx_cat_filter_module);
	
	if(conf->enable == 1){
		response_start_msec = ngx_current_msec;
	}
	
	return ngx_http_next_header_filter(r);
}

	static ngx_int_t
ngx_cat_body_filter(ngx_http_request_t *r, ngx_chain_t *chain)
{
	ngx_http_cat_t  *conf;
	conf = ngx_http_get_module_loc_conf(r, ngx_cat_filter_module);

	if(conf->enable == 1){
		send_times ++;
		if(r->upstream->state->response_length){
			char buf[SENDOUTBUFSIZE];
			int p = 0;
			memset(buf, 0, SENDOUTBUFSIZE);
			unsigned int time = ngx_current_msec - (r->start_sec * 1000 + r->start_msec);
			char requesttime[40];
			memset(requesttime, 0, 40);
			sprintf(requesttime, "%u", time);
			put_string_to_buf(buf, "?request_time=", requesttime, &p);
	//		ngx_memcpy(buf + p, "?request_time=", strlen("?request_time="));
	//		p = p + strlen("?request_time=");
	//		sprintf(buf + p, "%u", time);
	//		p = strlen(buf);
			char upstreamtime[40];
			memset(upstreamtime, 0, 40);
			sprintf(upstreamtime, "%lu", r->upstream->state->response_sec * 1000 + r->upstream->state->response_msec);
			put_string_to_buf(buf, "&upstream_time=", upstreamtime, &p);
//			ngx_memcpy(buf + p, "&upstream_time=", strlen("&upstream_time="));
//			p = p + strlen("&upstream_time=");
//			sprintf(buf + p, "%lu", r->upstream->state->response_sec * 1000 + r->upstream->state->response_msec);
//			p = strlen(buf);
			char upstream_start_sec_buf[40];
			memset(upstream_start_sec_buf, 0, 40);
			sprintf(upstream_start_sec_buf, "%lu", start_upstream_sec);
			put_string_to_buf(buf, "&upstream_start_sec=", upstream_start_sec_buf, &p);
			start_upstream_sec = 0;

			char upstream_start_msec_buf[40];
			memset(upstream_start_msec_buf, 0, 40);
			sprintf(upstream_start_msec_buf, "%lu", start_upstream_msec);
			put_string_to_buf(buf, "&upstream_start_msec=", upstream_start_msec_buf, &p);
			start_upstream_msec = 0;
			
			char response_start_msec_buf[40];
			memset(response_start_msec_buf, 0, 40);
			sprintf(response_start_msec_buf, "%u", response_start_msec);
			put_string_to_buf(buf, "&upstream_response_start_msec=", response_start_msec_buf, &p);
			response_start_msec = 0;
			
			ngx_memcpy(buf + p, "&start_sec=", strlen("&start_sec="));
			p = p + strlen("&start_sec=");
			sprintf(buf + p, "%u", (unsigned int)r->start_sec);
			p = strlen(buf);
	
			ngx_memcpy(buf + p, "&start_msec=", strlen("&start_msec="));
			p = p + strlen("&start_msec=");
			sprintf(buf + p, "%u", (unsigned int)r->start_msec);
			p = strlen(buf);
			
			ngx_memcpy(buf + p, "&status=", strlen("&status="));
			p = p + strlen("&status=");
			sprintf(buf + p, "%u", (unsigned int)r->headers_out.status);
			p = strlen(buf);

			ngx_memcpy(buf + p, "&response_body_len=", strlen("&response_body_len="));
			p = p + strlen("&response_body_len=");
			sprintf(buf + p, "%u", (unsigned int)r->upstream->state->response_length);
			p = strlen(buf);

			ngx_memcpy(buf + p, "&response_header_len=", strlen("&response_header_len="));
			p = p + strlen("&response_header_len=");
			sprintf(buf + p, "%d", (int)r->header_size);
			p = strlen(buf);

			ngx_memcpy(buf + p, "&request_header_len=", strlen("&request_header_len="));
			p = p + strlen("&request_header_len=");
			sprintf(buf + p, "%u", (unsigned int)r->request_length);
			p = strlen(buf);

			char send_times_buf[40];
			memset(send_times_buf, 0, 40);
			sprintf(send_times_buf, "%u", send_times - 1);
			put_string_to_buf(buf, "&send_times=", send_times_buf, &p);
			send_times = 0;
			
			size_t i;
			for(i = 0; i < r->headers_out.headers.part.nelts; i++){
				if(!ngx_strncmp(((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.data, CATPREFIX, strlen(CATPREFIX))){
					ngx_memcpy(buf + p, "&", 1);
					p++;
					ngx_memcpy(buf + p, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.data, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.len);
					p = p + ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.len;
					ngx_memcpy(buf + p, "=", 1);
					p++;
					ngx_memcpy(buf + p, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.data, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len );
					p = p + ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len;
				}
			}
			
			write(pipefd[1], buf, SENDOUTBUFSIZE);
		}
		else{
			if(!start_upstream_sec){
				start_upstream_sec = r->upstream->state->response_sec;
				start_upstream_msec = r->upstream->state->response_msec;
			}
		}
	}
//	ngx_buf_t    *b;    
//	b = ngx_calloc_buf(r->pool);    
//	if (b == NULL) {
//        return NGX_ERROR;    
//	}
//    b->pos = (u_char *) "<!-- Served by Nginx -->";
//    b->last = b->pos + sizeof("<!-- Served by Nginx -->") - 1;
//	b->memory = 1;
//    ngx_chain_t   *added_link;    
//	added_link = ngx_alloc_chain_link(r->pool);   
//	if (added_link == NULL)        
//		return NGX_ERROR;    
//	added_link->buf = b;    
//	added_link->next = NULL;
//    chain->next = added_link;
//    chain->buf->last_buf = 0;
	return ngx_http_next_body_filter(r, chain);
}

	static void *
ngx_cat_create_conf(ngx_conf_t *cf)
{
	ngx_http_cat_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cat_t));
	if (conf == NULL) {
		return NULL;
	}

	/*
	 * set by ngx_pcalloc():
	 *
	 *     conf->bufs.num = 0;
	 *     conf->types = { NULL };
	 *     conf->types_keys = NULL;
	 */

	conf->enable = NGX_CONF_UNSET;
	return conf;
}


	static char *
ngx_cat_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_cat_t *prev = parent;
	ngx_http_cat_t *conf = child;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	return NGX_CONF_OK;
}


static char * ngx_http_cat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
	ngx_http_cat_t	*cat = conf;
	if (pipe(pipefd) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	//fcntl(pipefd[1],F_SETFL,O_NONBLOCK);
	cat->enable = 1;

	return NGX_CONF_OK;
}

