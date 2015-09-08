#include "kernel.h"
#include "x86/x86.h"
#include "tty.h"
#include "string.h"
#include "term.h"
#include "time.h"

Console ttys[NR_TTY];
Console *current_consl;

static const char *ttynames[NR_TTY] = {"tty1", "tty2", "tty3", "tty4"};
static uint16_t *vmem = (void*)pa_to_va(0xb8000);

static uint16_t vbuf[NR_TTY][SCR_W * SCR_H * 2];

char banner[SCR_W + 1];

const char*
get_current_tty(void) {
	return current_consl->name;
}

static uint16_t
draw(char ch) {
	return (C_BLACK << 12) | (C_WHITE << 8) | ch;
}

static void
scrup(Console *c) {
	int i;
	if (c->scr + c->wh >= c->vbuf + c->size) {
		for (i = c->w; i < c->size; i ++)
			c->vbuf[i - c->w] = c->vbuf[i];
		c->scr -= c->w;
		c->pos -= c->w;
	}
	for (i = 0; i < c->w; i ++)
		c->scr[c->wh + i] = draw(' ');
	c->scr += c->w;
}

static void
next(Console *c) {
	if (c->vbuf + c->pos == c->scr + c->wh - 1) {
		scrup(c);
	}
	c->pos ++;
}

static void
putc(Console *c, char ch) {
	*(c->vbuf + c->pos) = draw(ch);
}

static void
cr(Console *c) {
	c->pos -= c->pos % c->w;
}

static void
lf(Console *c) {
	c->pos += c->w;
	if (c->pos >= c->scr - c->vbuf + c->wh) {
		scrup(c);
	}
}

static void
del(Console *c) {
	c->pos --;
	putc(c, ' ');
}

static bool
movl(Console *c) {
	if (c->i != 0) {
		c->i --;
		c->pos --;
		return true;
	}
	return false;
}

static bool
movr(Console *c) {
	if (c->lbuf[c->i] != 0) {
		c->i ++;
		c->pos ++;
		return true;
	}
	return false;
}

void
consl_sync(Console *c) {
	int i;
	for (i = 0; i < SCR_W; i ++) {
		vmem[i] = (C_BLUE << 12) | (C_LWHITE << 8) | banner[i];
	}
	if (current_consl == c) {
		memcpy(vmem + c->w, c->scr, c->wh * 2);
		int pos = c->pos - (c->scr - c->vbuf) + c->w;
		out_byte(0x3d4, 0xe);
		out_byte(0x3d5, pos >> 8);
		out_byte(0x3d4, 0xf);
		out_byte(0x3d5, pos & 0xff);
	}
}

static void
removec(Console *c) {
	char *ptr = &c->lbuf[-- c->i];
	uint16_t *scr = &c->vbuf[c->pos - 1];
	while (*ptr != 0) {
		*ptr = *(ptr + 1);
		*scr = *(scr + 1);
		ptr ++; scr ++;
	}
}

static void
backsp(Console *c) {
	if (c->i > 0) {
		removec(c);
		if (c->lbuf[c->i] == 0) {
			del(c);
		} else {
			c->pos --;
		}
	}
	else {
		/* Optional: Insert code here to play some sound */

	}
}

static size_t
get_cooked(Console *c, pid_t pid, char *buf, int count) {
	assert(c->f != c->r);
	int nread = 0;
	while (count --) {
		if (c->cbuf[c->f] == 0) {
			c->f = (c->f + 1) % CBUF_SZ;
			break;
		}
		copy_from_kernel(fetch_pcb(pid), buf ++, c->cbuf + c->f, 1);
		nread ++;
		c->f = (c->f + 1) % CBUF_SZ;
	}
	return nread;
}

void
read_request(Msg *m) {
	Console *c = &ttys[m->dev_id];
	if (c->f == c->r) {
		if (c->rtop == RSTK_SZ) panic("too many read request");
		memcpy(&c->rstk[c->rtop ++], m, sizeof(Msg));
	} else {
		int nread = get_cooked(c, m->req_pid, m->buf, m->len);
		m->ret = nread;
		pid_t dest = m->src;
		m->src = TTY;
		send(dest, m);
	}
}

static void
cook(Console *c) {
	printk("Capture: %s\n", c->lbuf);
	char *p = c->lbuf;
	do {
		c->cbuf[c->r] = *p;
		c->r = (c->r + 1) % CBUF_SZ;
		if (c->r == c->f) panic("cooked buffer full");
	} while (*p ++ != 0);
	c->lbuf[c->i = 0] = 0;
	cr(c); lf(c);
	if (c->rtop != 0) {
		c->rtop --;
		read_request(&c->rstk[c->rtop]);
	}
}

void
consl_writec(Console *c, char ch) {
	switch (ch) {
		case '\r': cr(c); break;
		case '\n': cr(c); lf(c); break;
		default:
			putc(c, ch);
			next(c);
	}
}

void
consl_accept(Console *c, char ch) {
	int i, cc = 0;
	for (; c->lbuf[c->i + cc] != 0; cc ++);
	for (i = cc + 1; i > 0; i --) {
		if (c->i + i >= LBUF_SZ) panic("line buffer full");
		c->lbuf[c->i + i] = c->lbuf[c->i + i - 1];
		if (c->vbuf + c->pos + i == c->scr + c->wh + 1) {
			scrup(c);
		}
		c->vbuf[c->pos + i] = c->vbuf[c->pos + i - 1];
	}
	putc(c, ch);
	next(c);
	if (c->i >= LBUF_SZ) panic("line buffer full");
	c->lbuf[c->i ++] = ch;
	consl_sync(c);
}

void
consl_feed(Console *c, int key) {
	switch (key) {
		case K_ENTR:
			cook(c);
			break;
		case K_DEL:
		case K_BACK:
			if (key == K_DEL) movr(c);
			backsp(c);
			break;
		case K_LEFT:
			movl(c);
			break;
		case K_RIGHT:
			movr(c);
			break;
		case K_HOME:
			while (movl(c));
			break;
		case K_END:
			while (movr(c));
			break;
		case K_F1: case K_F2: case K_F3:
		case K_F4: case K_F5: case K_F6:
			if (key - K_F1 < NR_TTY) {
				c = current_consl = ttys + key - K_F1;
			}
			break;
	}
	consl_sync(c);
}

static void
init_consl(int tty_index) {
	Console *c = ttys + tty_index;
	c->name = ttynames[tty_index];
	c->vbuf = vbuf[tty_index];
	c->scr = vbuf[tty_index];
	c->size = SCR_W * SCR_H * 2;
	c->pos = 0;
	c->w = SCR_W;
	c->h = SCR_H;
	c->wh = c->w * c->h;
	c->lbuf[0] = 0;

	int i;
	for (i = 0; i < SCR_W * SCR_H; i ++) {
		c->scr[i] = draw(' ');
	}
	c->i = c->r = c->f = 0;
	c->rtop = 0;
	consl_sync(c);
}

static void
send_updatemsg(void) {
	if (get_jiffy() % (HZ / 10) == 0) {
		Msg m;
		m.src = MSG_HARD_INTR;
		m.type = MSG_TTY_UPDATE;
		send(TTY, &m);
	}
}

void init_console(void) {
	memset(banner, ' ', sizeof(banner));
	banner[SCR_W] = 0;
	int i;
	for (i = 0; i < NR_TTY; i ++) {
		init_consl(i);
		hal_register(ttys[i].name, TTY, i);
	}
	current_consl = ttys;
	add_irq_handle(0, send_updatemsg);
}

