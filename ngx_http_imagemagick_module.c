/*
 * ngx_http_imagemagick_module.c
 *
 *  Created on: Jan 2, 2014
 *      Author: lidaohang
 */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_md5.h>
#include <ngx_crc32.h>
#include <ngx_palloc.h>
#include <ngx_string.h>
#include <stdlib.h>
#include <wand/MagickWand.h>

typedef struct {
	//ngx_int_t imagemagick;
	ngx_flag_t enable;

} ngx_http_imagemagick_loc_conf_t;

typedef struct {
	char* data;
	size_t len;
} ngx_image_data_t;

typedef struct {
	size_t height;
	size_t width;
	double quality;
	size_t watermark;
	char* path;
} ngx_image_file_t;

static ngx_int_t ngx_http_imagemagick_init(ngx_conf_t *cf);

static void *ngx_http_imagemagick_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_imagemagick(ngx_conf_t *cf, ngx_command_t *cmd,
		void *conf);

static ngx_command_t ngx_http_imagemagick_commands[] = { {
//定义配置指令的名称
		ngx_string("imagemagick"), NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF
				| NGX_HTTP_LOC_CONF | NGX_CONF_FLAG, ngx_http_imagemagick,
		//存入loc_conf
		NGX_HTTP_LOC_CONF_OFFSET,
		//offsetof(指定该配置项值的精确存放位置，一般指定为某一个结构体变量的字段偏移)
		offsetof(ngx_http_imagemagick_loc_conf_t, enable), NULL },
		ngx_null_command };

//一般情况下，我们自定义的模块，大多数是挂载在NGX_HTTP_CONTENT_PHASE阶段的。挂载的动作一般是在模块上下文调用的postconfiguration函数中。
static ngx_http_module_t ngx_http_imagemagick_module_ctx = { NULL, /* preconfiguration */
ngx_http_imagemagick_init, /* postconfiguration (在创建和读取该模块的配置信息之后被调用)*/

NULL, /* create main configuration */
NULL, /* init main configuration */

NULL, /* create server configuration */
NULL, /* merge server configuration */

ngx_http_imagemagick_create_loc_conf, /* create location configuration */
NULL /* merge location configuration */
};

ngx_module_t ngx_http_imagemagick_module = { NGX_MODULE_V1,
		&ngx_http_imagemagick_module_ctx, /* module context */
		ngx_http_imagemagick_commands, /* module directives */
		NGX_HTTP_MODULE, /* module type */
		NULL, /* init master */
		NULL, /* init module */
		NULL, /* init process */
		NULL, /* init thread */
		NULL, /* exit thread */
		NULL, /* exit process */
		NULL, /* exit master */
		NGX_MODULE_V1_PADDING };

static void readImage(MagickWand *m_wand, const char* path) {
	MagickReadImage(m_wand, path);
}

/*
 * Search and replace a string with another string , in a string
 * */
static char *str_replace(char *search, char *replace, char *subject) {
	char *p = NULL, *old = NULL, *new_subject = NULL;
	int c = 0, search_size;

	search_size = strlen(search);

	//Count how many occurences
	for (p = strstr(subject, search); p != NULL ;
			p = strstr(p + search_size, search)) {
		c++;
	}

	//Final size
	c = (strlen(replace) - search_size) * c + strlen(subject);

	//New subject with new size
	new_subject = malloc(c);

	//Set it to blank
	strcpy(new_subject, "");

	//The start position
	old = subject;

	for (p = strstr(subject, search); p != NULL ;
			p = strstr(p + search_size, search)) {
		//move ahead and copy some text from original subject , from a certain position
		strncpy(new_subject + strlen(new_subject), old, p - old);

		//move ahead and copy the replacement text
		strcpy(new_subject + strlen(new_subject), replace);

		//The new start position after this search match
		old = p + search_size;
	}

	//Copy the part after the last search match
	strcpy(new_subject + strlen(new_subject), old);

	return new_subject;
}
static void resizeImage(MagickWand *m_wand, size_t width, size_t height) {
	// Resize the image using the Lanczos filter
	// The blur factor is a "double", where > 1 is blurry, < 1 is sharp
	// I haven't figured out how you would change the blur parameter of MagickResizeImage
	// on the command line so I have set it to its default of one.
	MagickResizeImage(m_wand, width, height, LanczosFilter, 1);
}

static void qualityImage(MagickWand *m_wand, double quality) {
	// Set the compression quality to 95 (high quality = low compression)
	MagickSetImageCompressionQuality(m_wand, quality);
}

static char *str_copy(const char *str) {
	int len = strlen(str) + 1;
	char *buf = malloc(len);
	if (NULL == buf)
		return NULL ;
	memcpy(buf, str, len);
	return buf;
}

static int strsplit(const char *str, char *parts[], const char *delimiter) {
	char *pch;
	int i = 0;
	char *tmp = str_copy(str);
	pch = strtok(tmp, delimiter);

	parts[i++] = str_copy(pch);

	while (pch) {
		pch = strtok(NULL, delimiter);
		if (NULL == pch)
			break;
		parts[i++] = str_copy(pch);
	}

	free(tmp);
	free(pch);
	return i;
}

static ngx_image_data_t * ngx_image_resize(ngx_image_file_t *images) {
	ngx_image_data_t *images_data_t;
	images_data_t = malloc(sizeof(ngx_image_data_t));

	MagickWand *m_wand = NULL;
	MagickWandGenesis();

	m_wand = NewMagickWand();
	readImage(m_wand, images->path);

	resizeImage(m_wand, images->width, images->height);

	// Set the compression quality to 95 (high quality = low compression)
	if(images->quality==0){
		images->quality=90;
	}
	qualityImage(m_wand, images->quality);

	//data
	size_t image_length = 0;
	unsigned char* data = MagickGetImageBlob(m_wand, &image_length);

	/* Clean up */
	if (m_wand)
		m_wand = DestroyMagickWand(m_wand);

	MagickWandTerminus();

	free(images->path);
	free(images);

	images_data_t->data = (char *) data;
	images_data_t->len = image_length;

	return images_data_t;
}

static ngx_image_file_t * ngx_get_image(ngx_http_request_t *r) {

	ngx_image_file_t *images;
	images = malloc(sizeof(ngx_image_file_t));

	//get uri
	ngx_str_t uri = r->unparsed_uri;
	u_char* ngx_string_uri;

	ngx_string_uri = ngx_pcalloc(r->pool, (uri.len + 1) * sizeof(u_char));
	ngx_memset(ngx_string_uri, 0, sizeof(u_char) * (uri.len + 1));
	ngx_sprintf(ngx_string_uri, "%V", &uri);

	//split uri  /data/logo/80x60_30_1_logo.jpg
	char **ngx_array_uri = calloc(1, sizeof(char));
	size_t uri_size = strsplit((const char *) ngx_string_uri, ngx_array_uri,
			"/");

	//split filename  80x60_30_1_logo.jpg
	char **ngx_array_image = calloc(1, sizeof(char));
	size_t image_size = strsplit(ngx_array_uri[uri_size - 1], ngx_array_image,
			"_");

	char* string_repl;
	string_repl = ngx_pcalloc(r->pool, sizeof(char) * 1024);
	strcpy(string_repl, "");

	if (image_size >= 2) {
		char ** ngx_array_image_size = calloc(1, sizeof(char));
		size_t image_size = strsplit(ngx_array_image[0], ngx_array_image_size,
				"x");

		// width hegith
		if (image_size == 2) {
			images->width = atoi(ngx_array_image_size[0]);
			images->height = atoi(ngx_array_image_size[1]);
		}
		strcat(string_repl, ngx_array_image[0]);
		strcat(string_repl, "_");
	}
	if (image_size >= 3) {
		//quality
		images->quality = atof(ngx_array_image[1]);
		strcat(string_repl, ngx_array_image[1]);
		strcat(string_repl, "_");
	}
	if (image_size > 3) {
		images->watermark = atoi(ngx_array_image[2]);
		strcat(string_repl, ngx_array_image[2]);
		strcat(string_repl, "_");
	}

	char* new_uri = str_replace(string_repl, "", (char *)ngx_string_uri);
	images->path = malloc((strlen(new_uri) + 1) * sizeof(char));
	ngx_cpystrn((u_char *)images->path,(u_char *)new_uri,(strlen(new_uri) + 1)* sizeof(char));
	free(new_uri);
	return images;
}

static ngx_int_t ngx_http_imagemagick_handler(ngx_http_request_t *r) {

	ngx_int_t rc;
	ngx_buf_t *b;
	ngx_chain_t out;
	ngx_uint_t content_length = 0;

	ngx_image_file_t *images_info_t = ngx_get_image(r);

	ngx_image_data_t *images_data_t = ngx_image_resize(images_info_t);

	size_t ngx_data_len=images_data_t->len;
	u_char * ngx_data = ngx_pcalloc(r->pool, ngx_data_len*sizeof(u_char));
	ngx_memcpy(ngx_data, images_data_t->data, ngx_data_len);

	free(images_data_t->data);
	free(images_data_t);


	content_length = ngx_data_len;

	/* we response to 'GET' and 'HEAD' requests only */
	if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
		return NGX_HTTP_NOT_ALLOWED;
	}

	/* discard request body, since we don't need it here */
	rc = ngx_http_discard_request_body(r);

	if (rc != NGX_OK) {
		return rc;
	}

	/* set the 'Content-type' header */
	/*
	 r->headers_out.content_type_len = sizeof("text/html") - 1;
	 r->headers_out.content_type.len = sizeof("text/html") - 1;
	 r->headers_out.content_type.data = (u_char *)"text/html";*/
	//ngx_str_set(&r->headers_out.content_type, "text/html");
	r->headers_out.content_type.len = sizeof("image/jpeg") - 1;
	r->headers_out.content_type.data = (u_char *) "image/jpeg";

	/* send the header only, if the request type is http 'HEAD' */
	if (r->method == NGX_HTTP_HEAD) {
		r->headers_out.status = NGX_HTTP_OK;
		r->headers_out.content_length_n = content_length;

		return ngx_http_send_header(r);
	}

	/* allocate a buffer for your response body */
	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	/* attach this buffer to the buffer chain */
	out.buf = b;
	out.next = NULL;

	/* adjust the pointers of the buffer */
	b->pos = (u_char *) ngx_data;
	b->last = (u_char *) ngx_data + content_length;
	b->memory = 1; /* this buffer is in memory */
	b->last_buf = 1; /* this is the last buffer in the buffer chain */

	/* set the status line */
	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = content_length;

	/* send the headers of your response */
	rc = ngx_http_send_header(r);

	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		return rc;
	}


	/* send the buffer chain of your response */
	return ngx_http_output_filter(r, &out);
}

static void *ngx_http_imagemagick_create_loc_conf(ngx_conf_t *cf) {
	ngx_http_imagemagick_loc_conf_t* local_conf = NULL;
	local_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_imagemagick_loc_conf_t));
	if (local_conf == NULL ) {
		return NULL ;
	}

	local_conf->enable = NGX_CONF_UNSET;

	return local_conf;
}

static char *ngx_http_imagemagick(ngx_conf_t *cf, ngx_command_t *cmd,
		void *conf) {
	ngx_http_core_loc_conf_t *clcf;

	//	/* set handler */
	clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module) ;

	clcf->handler = ngx_http_imagemagick_handler;

	return NGX_CONF_OK ;
}

static ngx_int_t ngx_http_imagemagick_init(ngx_conf_t *cf) {
	ngx_http_handler_pt *h;
	ngx_http_core_main_conf_t *cmcf;

	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module) ;

	h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
	if (h == NULL ) {
		return NGX_ERROR;
	}

	*h = ngx_http_imagemagick_handler;

	return NGX_OK;
}

