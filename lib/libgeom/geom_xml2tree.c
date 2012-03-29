/*-
 * Copyright (c) 2003 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <err.h>
#include <bsdxml.h>
#include <libgeom.h>

struct mystate {
	struct gmesh		*mesh;
	struct gclass		*class;
	struct ggeom		*geom;
	struct gprovider	*provider;
	struct gconsumer	*consumer;
	int			level;
	struct sbuf		*sbuf[20];
	struct gconf		*config;
	unsigned		nident;
};

static void *
internalize_ident(struct mystate *mt, const char *element, const char *str)
{
	struct gident *gip;
	unsigned i;

	if (mt->nident != 0 && mt->mesh->lg_ident == NULL) {
		warn("Cannot continue due to previous memory exhaustion.");
		return (NULL);
	}

	for (i = 0; i < mt->nident; i++) {
		if (strcmp(mt->mesh->lg_ident[i].lg_id, str) != 0)
			continue;
		return ((void *)(uintptr_t)(i + 1));
	}

	i = mt->nident;
	mt->nident++;
	mt->mesh->lg_ident = reallocf(mt->mesh->lg_ident, (mt->nident + 1) * sizeof mt->mesh->lg_ident[0]);
	if (mt->mesh->lg_ident == NULL) {
		warn("Cannot allocate memory during processing of '%s' "
		    "element for identifier '%s'", element, str);
		return (NULL);
	}

	gip = &mt->mesh->lg_ident[i];
	gip->lg_id = strdup(str);
	if (gip->lg_id == NULL) {
		free(mt->mesh->lg_ident);
		mt->mesh->lg_ident = NULL;
		warn("Cannot allocate memory during processing of '%s' "
		    "element for identifier '%s'", element, str);
		return (NULL);
	}
	gip->lg_ptr = NULL;
	gip->lg_what = ISUNRESOLVED;

	/* Terminator entry.  */
	gip = &mt->mesh->lg_ident[i + 1];
	gip->lg_id = NULL;
	gip->lg_ptr = NULL;
	gip->lg_what = ISUNRESOLVED;

	return ((void *)(uintptr_t)(i + 1));
}

static void
StartElement(void *userData, const char *name, const char **attr)
{
	struct mystate *mt;
	void *id;
	void *ref;
	int i;

	mt = userData;
	mt->level++;
	mt->sbuf[mt->level] = sbuf_new_auto();
	id = NULL;
	ref = NULL;
	for (i = 0; attr[i] != NULL; i += 2) {
		if (!strcmp(attr[i], "id")) {
			id = internalize_ident(mt, name, attr[i + 1]);
		} else if (!strcmp(attr[i], "ref")) {
			ref = internalize_ident(mt, name, attr[i + 1]);
		} else
			printf("%*.*s[%s = %s]\n",
			    mt->level + 1, mt->level + 1, "",
			    attr[i], attr[i + 1]);
	}
	if (!strcmp(name, "class") && mt->class == NULL) {
		mt->class = calloc(1, sizeof *mt->class);
		if (mt->class == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		mt->class->lg_id = id;
		LIST_INSERT_HEAD(&mt->mesh->lg_class, mt->class, lg_class);
		LIST_INIT(&mt->class->lg_geom);
		LIST_INIT(&mt->class->lg_config);
		return;
	}
	if (!strcmp(name, "geom") && mt->geom == NULL) {
		mt->geom = calloc(1, sizeof *mt->geom);
		if (mt->geom == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		mt->geom->lg_id = id;
		LIST_INSERT_HEAD(&mt->class->lg_geom, mt->geom, lg_geom);
		LIST_INIT(&mt->geom->lg_provider);
		LIST_INIT(&mt->geom->lg_consumer);
		LIST_INIT(&mt->geom->lg_config);
		return;
	}
	if (!strcmp(name, "class") && mt->geom != NULL) {
		mt->geom->lg_class = ref;
		return;
	}
	if (!strcmp(name, "consumer") && mt->consumer == NULL) {
		mt->consumer = calloc(1, sizeof *mt->consumer);
		if (mt->consumer == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		mt->consumer->lg_id = id;
		LIST_INSERT_HEAD(&mt->geom->lg_consumer, mt->consumer,
		    lg_consumer);
		LIST_INIT(&mt->consumer->lg_config);
		return;
	}
	if (!strcmp(name, "geom") && mt->consumer != NULL) {
		mt->consumer->lg_geom = ref;
		return;
	}
	if (!strcmp(name, "provider") && mt->consumer != NULL) {
		mt->consumer->lg_provider = ref;
		return;
	}
	if (!strcmp(name, "provider") && mt->provider == NULL) {
		mt->provider = calloc(1, sizeof *mt->provider);
		if (mt->provider == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		mt->provider->lg_id = id;
		LIST_INSERT_HEAD(&mt->geom->lg_provider, mt->provider,
		    lg_provider);
		LIST_INIT(&mt->provider->lg_consumers);
		LIST_INIT(&mt->provider->lg_config);
		return;
	}
	if (!strcmp(name, "geom") && mt->provider != NULL) {
		mt->provider->lg_geom = ref;
		return;
	}
	if (!strcmp(name, "config")) {
		if (mt->provider != NULL) {
			mt->config = &mt->provider->lg_config;
			return;
		}
		if (mt->consumer != NULL) {
			mt->config = &mt->consumer->lg_config;
			return;
		}
		if (mt->geom != NULL) {
			mt->config = &mt->geom->lg_config;
			return;
		}
		if (mt->class != NULL) {
			mt->config = &mt->class->lg_config;
			return;
		}
	}
}

static void
EndElement(void *userData, const char *name)
{
	struct mystate *mt;
	struct gconfig *gc;
	char *p;

	mt = userData;
	sbuf_finish(mt->sbuf[mt->level]);
	p = strdup(sbuf_data(mt->sbuf[mt->level]));
	if (p == NULL) {
		warn("Cannot allocate memory during processing of '%s' "
		    "element", name);
		return;
	}
	sbuf_delete(mt->sbuf[mt->level]);
	mt->sbuf[mt->level] = NULL;
	mt->level--;
	if (strlen(p) == 0) {
		free(p);
		p = NULL;
	}

	if (!strcmp(name, "name")) {
		if (mt->provider != NULL) {
			mt->provider->lg_name = p;
			return;
		} else if (mt->geom != NULL) {
			mt->geom->lg_name = p;
			return;
		} else if (mt->class != NULL) {
			mt->class->lg_name = p;
			return;
		}
	}
	if (!strcmp(name, "rank") && mt->geom != NULL) {
		mt->geom->lg_rank = strtoul(p, NULL, 0);
		free(p);
		return;
	}
	if (!strcmp(name, "mode") && mt->provider != NULL) {
		mt->provider->lg_mode = p;
		return;
	}
	if (!strcmp(name, "mode") && mt->consumer != NULL) {
		mt->consumer->lg_mode = p;
		return;
	}
	if (!strcmp(name, "mediasize") && mt->provider != NULL) {
		mt->provider->lg_mediasize = strtoumax(p, NULL, 0);
		free(p);
		return;
	}
	if (!strcmp(name, "sectorsize") && mt->provider != NULL) {
		mt->provider->lg_sectorsize = strtoul(p, NULL, 0);
		free(p);
		return;
	}
	if (!strcmp(name, "stripesize") && mt->provider != NULL) {
		mt->provider->lg_stripesize = strtoumax(p, NULL, 0);
		free(p);
		return;
	}
	if (!strcmp(name, "stripeoffset") && mt->provider != NULL) {
		mt->provider->lg_stripeoffset = strtoumax(p, NULL, 0);
		free(p);
		return;
	}

	if (!strcmp(name, "config")) {
		mt->config = NULL;
		return;
	}

	if (mt->config != NULL) {
		gc = calloc(1, sizeof *gc);
		if (gc == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		gc->lg_name = strdup(name);
		if (gc->lg_name == NULL) {
			warn("Cannot allocate memory during processing of '%s' "
			    "element", name);
			return;
		}
		gc->lg_val = p;
		LIST_INSERT_HEAD(mt->config, gc, lg_config);
		return;
	}

	if (p != NULL) {
		printf("Unexpected XML: name=%s data=\"%s\"\n", name, p);
		free(p);
	}

	if (!strcmp(name, "consumer") && mt->consumer != NULL) {
		mt->consumer = NULL;
		return;
	}
	if (!strcmp(name, "provider") && mt->provider != NULL) {
		mt->provider = NULL;
		return;
	}
	if (!strcmp(name, "geom") && mt->consumer != NULL) {
		return;
	}
	if (!strcmp(name, "geom") && mt->provider != NULL) {
		return;
	}
	if (!strcmp(name, "geom") && mt->geom != NULL) {
		mt->geom = NULL;
		return;
	}
	if (!strcmp(name, "class") && mt->geom != NULL) {
		return;
	}
	if (!strcmp(name, "class") && mt->class != NULL) {
		mt->class = NULL;
		return;
	}
}

static void
CharData(void *userData , const XML_Char *s , int len)
{
	struct mystate *mt;
	const char *b, *e;

	mt = userData;

	b = s;
	e = s + len - 1;
	while (isspace(*b) && b < e)
		b++;
	while (isspace(*e) && e > b)
		e--;
	if (e != b || (*b && !isspace(*b)))
		sbuf_bcat(mt->sbuf[mt->level], b, e - b + 1);
}

struct gident *
geom_lookupid(struct gmesh *gmp, const void *id)
{
	unsigned i;

	if (gmp->lg_ident == NULL)
		return (NULL);

	for (i = 0; gmp->lg_ident[i].lg_id != NULL; i++) {
		if (i + 1 != (unsigned)(uintptr_t)id)
			continue;
		return (&gmp->lg_ident[i]);
	}
	return (NULL);
}

int
geom_xml2tree(struct gmesh *gmp, char *p)
{
	XML_Parser parser;
	struct mystate *mt;
	struct gclass *cl;
	struct ggeom *ge;
	struct gprovider *pr;
	struct gconsumer *co;
	struct gident *gip;
	int i;

	memset(gmp, 0, sizeof *gmp);
	LIST_INIT(&gmp->lg_class);
	parser = XML_ParserCreate(NULL);
	if (parser == NULL)
		return (ENOMEM);
	mt = calloc(1, sizeof *mt);
	if (mt == NULL) {
		XML_ParserFree(parser);
		return (ENOMEM);
	}
	mt->mesh = gmp;
	XML_SetUserData(parser, mt);
	XML_SetElementHandler(parser, StartElement, EndElement);
	XML_SetCharacterDataHandler(parser, CharData);
	i = XML_Parse(parser, p, strlen(p), 1);
	XML_ParserFree(parser);
	if (i != 1) {
		free(mt);
		return (-1);
	}
	if (gmp->lg_ident == NULL && mt->nident != 0) {
		free(mt);
		return (ENOMEM);
	}
	free(mt);
	/* Collect all identifiers */
	LIST_FOREACH(cl, &gmp->lg_class, lg_class) {
		gip = geom_lookupid(gmp, cl->lg_id);
		gip->lg_ptr = cl;
		gip->lg_what = ISCLASS;

		LIST_FOREACH(ge, &cl->lg_geom, lg_geom) {
			gip = geom_lookupid(gmp, ge->lg_id);
			gip->lg_ptr = ge;
			gip->lg_what = ISGEOM;
			LIST_FOREACH(pr, &ge->lg_provider, lg_provider) {
				gip = geom_lookupid(gmp, pr->lg_id);
				gip->lg_ptr = pr;
				gip->lg_what = ISPROVIDER;
			}
			LIST_FOREACH(co, &ge->lg_consumer, lg_consumer) {
				gip = geom_lookupid(gmp, co->lg_id);
				gip->lg_ptr = co;
				gip->lg_what = ISCONSUMER;
			}
		}
	}
	/* Substitute all identifiers */
	LIST_FOREACH(cl, &gmp->lg_class, lg_class) {
		LIST_FOREACH(ge, &cl->lg_geom, lg_geom) {
			ge->lg_class =
			    geom_lookupid(gmp, ge->lg_class)->lg_ptr;
			LIST_FOREACH(pr, &ge->lg_provider, lg_provider) {
				pr->lg_geom =
				    geom_lookupid(gmp, pr->lg_geom)->lg_ptr;
			}
			LIST_FOREACH(co, &ge->lg_consumer, lg_consumer) {
				co->lg_geom =
				    geom_lookupid(gmp, co->lg_geom)->lg_ptr;
				if (co->lg_provider != NULL) {
					co->lg_provider = 
					    geom_lookupid(gmp,
						co->lg_provider)->lg_ptr;
					LIST_INSERT_HEAD(
					    &co->lg_provider->lg_consumers,
					    co, lg_consumers);
				}
			}
		}
	}
	return (0);
}

int
geom_gettree(struct gmesh *gmp)
{
	char *p;
	int error;

	p = geom_getxml();
	if (p == NULL)
		return (errno);
	error = geom_xml2tree(gmp, p);
	free(p);
	return (error);
}

static void 
delete_config(struct gconf *gp)
{
	struct gconfig *cf;

	for (;;) {
		cf = LIST_FIRST(gp);
		if (cf == NULL)
			return;
		LIST_REMOVE(cf, lg_config);
		free(cf->lg_name);
		free(cf->lg_val);
		free(cf);
	}
}

void
geom_deletetree(struct gmesh *gmp)
{
	struct gclass *cl;
	struct ggeom *ge;
	struct gprovider *pr;
	struct gconsumer *co;
	unsigned i;

	for (i = 0; gmp->lg_ident[i].lg_id != NULL; i++)
		free(gmp->lg_ident[i].lg_id);
	free(gmp->lg_ident);
	gmp->lg_ident = NULL;
	for (;;) {
		cl = LIST_FIRST(&gmp->lg_class);
		if (cl == NULL) 
			break;
		LIST_REMOVE(cl, lg_class);
		delete_config(&cl->lg_config);
		if (cl->lg_name) free(cl->lg_name);
		for (;;) {
			ge = LIST_FIRST(&cl->lg_geom);
			if (ge == NULL) 
				break;
			LIST_REMOVE(ge, lg_geom);
			delete_config(&ge->lg_config);
			if (ge->lg_name) free(ge->lg_name);
			for (;;) {
				pr = LIST_FIRST(&ge->lg_provider);
				if (pr == NULL) 
					break;
				LIST_REMOVE(pr, lg_provider);
				delete_config(&pr->lg_config);
				if (pr->lg_name) free(pr->lg_name);
				if (pr->lg_mode) free(pr->lg_mode);
				free(pr);
			}
			for (;;) {
				co = LIST_FIRST(&ge->lg_consumer);
				if (co == NULL) 
					break;
				LIST_REMOVE(co, lg_consumer);
				delete_config(&co->lg_config);
				if (co->lg_mode) free(co->lg_mode);
				free(co);
			}
			free(ge);
		}
		free(cl);
	}
}
