

/*
 * Copyright (C) peng hu
 * Copyright (C) DianPing, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#define SHARE_MEMORY 0


long push_num = 0;
#if SHARE_MEMORY
#define CAT_SHM_SIZE 1024*1024

typedef struct {
	ngx_flag_t          enable;
	ngx_shm_zone_t		*shm_zone; 
} ngx_http_cat_t;

typedef struct{
	ngx_queue_t			queue;
} ngx_cat_send_queue_t;

typedef struct{
	ngx_cat_send_queue_t		*shqueue;
	ngx_slab_pool_t             *shpool;
} ngx_cat_ctx_t;

typedef struct{
	ngx_int_t			cat_item;
	ngx_queue_t			queue;
} ngx_cat_node_t;

ngx_cat_ctx_t	*shctx;
ngx_queue_t		*shqueue;
ngx_slab_pool_t	*shpool;

#else
int pipefd[2]; 

typedef struct {
	ngx_flag_t          enable;
} ngx_http_cat_t;

#endif


static ngx_int_t ngx_cat_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_cat_header_filter(ngx_http_request_t *r);
static void * ngx_cat_create_conf(ngx_conf_t *cf);
static char * ngx_cat_merge_conf(ngx_conf_t *cf, void *parent, void *child);
#if SHARE_MEMORY
static ngx_int_t ngx_cat_init_zone(ngx_shm_zone_t *shm_zone, void *data);
#endif
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


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;

	static ngx_int_t
ngx_cat_filter_init(ngx_conf_t *cf)
{
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_cat_header_filter;

	return NGX_OK;
}

	static ngx_int_t
ngx_cat_header_filter(ngx_http_request_t *r)
{
	ngx_http_cat_t  *conf;
#if SHARE_MEMORY
	ngx_cat_node_t	*node;
	ngx_cat_ctx_t	*ctx;
	ctx = shctx;
#endif
	conf = ngx_http_get_module_loc_conf(r, ngx_cat_filter_module);
	
	if(conf->enable == 1){
		char buf[100];
		int p = 0;
		memset(buf, 0, 100);
	 	unsigned int time = ngx_current_msec - (r->start_sec * 1000 + r->start_msec);
		ngx_memcpy(buf + p, "?request_time=", strlen("?request_time="));
		p = p + strlen("?request_time=");
		sprintf(buf + p, "%u", time);
		p = strlen(buf);
		size_t i;
		for(i = 0; i < r->headers_out.headers.part.nelts; i++){
			if(!ngx_strncmp(((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.data,"X-CAT-ROOT-ID",((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.len)){
				ngx_memcpy(buf + p, "&X-CAT-ROOT-ID=", strlen("&X-CAT-ROOT-ID="));
				p = p + strlen("&X-CAT-ROOT-ID=");
				ngx_memcpy(buf + p, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.data, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len );
				p = p + ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len;
			}
			else if(!ngx_strncmp(((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.data,"X-CAT-PARENT-ID",((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.len)){
				ngx_memcpy(buf + p, "&X-CAT-PARENT-ID=", strlen("&X-CAT-PARENT-ID="));
				p = p + strlen("&X-CAT-PARENT-ID=");
				ngx_memcpy(buf + p, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.data, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len );
				p = p + ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len;
			}
			else if(!ngx_strncmp(((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.data,"X-CAT-ID",((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].key.len)){
				ngx_memcpy(buf + p, "&X-CAT-ID=", strlen("&X-CAT-ID="));
				p = p + strlen("&X-CAT-ID=");
				ngx_memcpy(buf + p, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.data, ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len );
				p = p + ((ngx_table_elt_t*)r->headers_out.headers.part.elts)[i].value.len;
			}
		}
#if SHARE_MEMORY
		node = ngx_slab_alloc(ctx->shpool, sizeof(ngx_cat_node_t));
		if(node == NULL){
			return ngx_http_next_header_filter(r);
		}
		node->cat_item = push_num++;
		ngx_queue_insert_tail(&ctx->shqueue->queue, &node->queue);
#else
		write(pipefd[1], buf, 100);
#endif
	}
	return ngx_http_next_header_filter(r);
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

#if SHARE_MEMORY
static ngx_int_t
ngx_cat_init_zone(ngx_shm_zone_t *shm_zone, void *data){

	shctx = shm_zone->data;

	shctx->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
	shctx->shqueue = ngx_slab_alloc(shctx->shpool, sizeof(ngx_cat_send_queue_t));
	if (shctx->shqueue == NULL) {
		return NGX_ERROR;
	}

	shctx->shpool->data = shctx->shqueue;
	ngx_queue_init(&shctx->shqueue->queue);
	shqueue = &(shctx->shqueue->queue);
	if(ngx_init_shqueue() == -1){
		return NGX_ERROR;
	}
	shpool = shctx->shpool;
	return NGX_OK;
}
#endif

static char * ngx_http_cat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
#if SHARE_MEMORY
	ngx_shm_zone_t	*shm_zone;
	ngx_cat_ctx_t	*ctx;
	ngx_str_t		s, *value;
	value = cf->args->elts;
	s.len = value[0].len;
	s.data = value[0].data;

	ctx = ngx_pcalloc(cf->pool, sizeof(ngx_cat_ctx_t));
	if (ctx == NULL) {
		return NGX_CONF_ERROR;
	}
#endif
	ngx_http_cat_t	*cat = conf;
#if SHARE_MEMORY
	shm_zone = NULL;
	shm_zone = ngx_shared_memory_add(cf, &s, CAT_SHM_SIZE, &ngx_cat_filter_module);

	if (shm_zone == NULL) {
		return NGX_CONF_ERROR;
	}

	shm_zone->init = ngx_cat_init_zone;
	shm_zone->data = ctx;
	cat->shm_zone = shm_zone;
#else
	if (pipe(pipefd) == -1) {
               perror("pipe");
               exit(EXIT_FAILURE);
    }
	//fcntl(pipefd[1],F_SETFL,O_NONBLOCK);
#endif
	cat->enable = 1;

	return NGX_CONF_OK;
}

