/*
 * The global NVRAM (Broadcom's nvram_ro.c): board variables read from a text
 * file "nvram.txt" of the host, kept as a list of blocks of NUL-separated
 * "name=value" strings, the newest block first.
 *
 * Written from docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM" and
 * "Notes for an implementation of nvram_ro.c". The list is this file's own:
 * one per driver, empty at load (the only state outside the arguments). Its
 * nodes, the work buffer and the copy of the file come from osl_malloc with
 * the sizes and in the order of the specification.
 */
#include <bcm4360/nvram.h>
#include <bcm4360/hw.h>

/* "nvram_ro.c: the global NVRAM": a node of the list (0x18 bytes) */
struct nvram_node {
	struct nvram_node *next;
	u32 hdr_size;		/* NVRAM_NODE_SIZE */
	u32 len;		/* bytes of data */
	char *data;		/* NUL-separated "name=value" strings */
};

#define NVRAM_NODE_SIZE		0x18	/* allocated and freed with this size */

_Static_assert(sizeof(struct nvram_node) <= NVRAM_NODE_SIZE, "node of srom-otp.md");

/* nvram_init: the bus type of the file, the file, the work buffer */
#define NVRAM_BUS_PCI		1
#define NVRAM_FILE		"nvram.txt"
#define NVRAM_BUF_SIZE		0x1000

/* results */
#define NVRAM_ERR		(-1)	/* nvram_init */
#define NVRAM_ERR_NOMEM		(-27)	/* nvram_append: no node */
#define NVRAM_ERR_BUFTOOSHORT	(-14)	/* nvram_getall */

/* the separator of name and value */
#define NVRAM_EQ		'='

/* the list: newest block first (srom-otp.md, "Notes ...": one per driver) */
static struct nvram_node *nvram_head;

/*
 * docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_init: the
 * bytes that become NUL - tab, space, LF and NUL.
 */
static bool nvram_is_sep(char c)
{
	return c == '\t' || c == ' ' || c == '\n' || c == '\0';
}

/*
 * docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_init: read
 * the open image into buf block by block (each to the start of buf: only the
 * last block survives), make the separators NUL, end it with a NUL (buf[n],
 * one past the buffer for a full block). Result: the bytes of the block with
 * that NUL.
 */
static s32 nvram_read_image(char *buf, void *img)
{
	s32 n = 0;
	s32 r;
	s32 i;

	while ((r = osl_os_get_image_block(buf, NVRAM_BUF_SIZE, img)) != 0)
		n = r;
	for (i = 0; i < n; i++)
		if (nvram_is_sep(buf[i]))
			buf[i] = '\0';
	buf[n] = '\0';
	return n + 1;
}

/* docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_init */
s32 bcm4360_nvram_init(void *osh, u32 bustype)
{
	s32 err = 0;
	char *buf;
	char *copy;
	void *img;
	s32 n;

	if (nvram_head || bustype != NVRAM_BUS_PCI)
		return 0;
	buf = osl_malloc(osh, NVRAM_BUF_SIZE);
	if (!buf)
		return NVRAM_ERR;
	img = osl_os_open_image(NVRAM_FILE);
	if (!img) {
		osl_mfree(osh, buf, NVRAM_BUF_SIZE);
		return NVRAM_ERR;
	}
	n = nvram_read_image(buf, img);
	osl_os_close_image(img);
	copy = osl_malloc(osh, (u32)n);
	if (copy) {
		hw_memcpy(copy, buf, (size_t)n);
		if (bcm4360_nvram_append(osh, copy, (u32)n) != 0)
			err = NVRAM_ERR;
	}
	osl_mfree(osh, buf, NVRAM_BUF_SIZE);
	return err;
}

/* docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_append */
s32 bcm4360_nvram_append(void *osh, char *data, u32 len)
{
	struct nvram_node *node;

	node = osl_malloc(osh, NVRAM_NODE_SIZE);
	if (!node)
		return NVRAM_ERR_NOMEM;
	node->next = nvram_head;
	node->hdr_size = NVRAM_NODE_SIZE;
	node->len = len;
	node->data = data;
	nvram_head = node;
	return 0;
}

/*
 * docs/re/spec/srom-otp.md, "Detail of nvram_get": the value of `name` in the
 * strings of one node, NULL if it has none. The walk ends at the first empty
 * string or when the next string would start at or after the end of the data.
 */
static char *nvram_find(const struct nvram_node *node, const char *name, s32 nlen)
{
	char *s = node->data;
	char *end = node->data + node->len;

	while (s < end && *s != '\0') {
		if (hw_memcmp(s, name, (size_t)nlen) == 0 && s[nlen] == NVRAM_EQ)
			return s + nlen + 1;
		s += hw_strlen(s) + 1;
	}
	return NULL;
}

/* docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_get */
char *bcm4360_nvram_get(const char *name)
{
	const struct nvram_node *node;
	s32 nlen = hw_strlen(name);
	char *v;

	for (node = nvram_head; node; node = node->next) {
		v = nvram_find(node, name, nlen);
		if (v)
			return v;
	}
	return NULL;
}

/*
 * docs/re/spec/srom-otp.md, "Detail of nvram_getall": copy the strings of one
 * node (each with its NUL) into buf, `resid` bytes of space. The walk is
 * bounded as that of nvram_get: it ends at the first empty string or when the
 * next string would start at or after the end of the data; a string that
 * starts inside the data is copied whole, up to its NUL. Result: the bytes
 * copied, or -14 as soon as a string does not fit (the strings copied before
 * it stay).
 */
static s32 nvram_copy_node(const struct nvram_node *node, char *buf, s32 resid)
{
	const char *s = node->data;
	const char *end = node->data + node->len;
	s32 total = 0;
	s32 len;

	while (s < end && *s != '\0') {
		len = hw_strlen(s) + 1;
		if (total + len > resid)
			return NVRAM_ERR_BUFTOOSHORT;
		hw_memcpy(buf + total, s, (size_t)len);
		total += len;
		s += len;
	}
	return total;
}

/* docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_getall */
s32 bcm4360_nvram_getall(char *buf, s32 count)
{
	const struct nvram_node *node;
	s32 resid = count;
	s32 total;

	for (node = nvram_head; node; node = node->next) {
		total = nvram_copy_node(node, buf, resid);
		if (total < 0)
			return total;
		resid -= total;
		buf += total;
	}
	if (resid < 1)
		return NVRAM_ERR_BUFTOOSHORT;
	*buf = '\0';
	return 0;
}

/*
 * docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM", nvram_exit: the
 * data of the head node only, then every node (with the header size it
 * holds); the list is empty afterwards.
 */
void bcm4360_nvram_exit(void *osh)
{
	struct nvram_node *node = nvram_head;
	struct nvram_node *next;

	if (node) {
		osl_mfree(osh, node->data, node->len);
		while (node) {
			next = node->next;
			osl_mfree(osh, node, node->hdr_size);
			node = next;
		}
	}
	nvram_head = NULL;
}

/*
 * docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM",
 * nvram_get_global_vars: the data and length of the head node (the list must
 * not be empty).
 */
void bcm4360_nvram_get_global_vars(char **vars, u32 *len)
{
	*vars = nvram_head->data;
	*len = nvram_head->len;
}
