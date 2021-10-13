/* prints a message using the bitmap font */
void print(SDL_Surface *dst, int x, int y, char *text)
{
	SDL_Rect pos;
	SDL_Surface *src;
	SDL_Color color = {255, 255, 255};
	if (font == NULL) return;
	pos.x = x;
	pos.y = y;
#ifdef ZIPIT_Z2_UTF8 
	src = TTF_RenderUTF8_Blended(font, text, color);
#else
	src = TTF_RenderText_Blended(font, text, color);
#endif
	SDL_BlitSurface(src, NULL, dst, &pos);
	SDL_FreeSurface(src);
}

/* prints a message using the bitmap font */
void printcolor(SDL_Surface *dst, int x, int y, char *text, int c)
{
	SDL_Rect pos;
	SDL_Surface *src;
	SDL_Color color = {0xff&(c>>16), 0xff&(c>>8), 0xff&(c)};
	if (font == NULL) return;
	pos.x = x;
	pos.y = y;
#ifdef ZIPIT_Z2_UTF8 
	src = TTF_RenderUTF8_Blended(font, text, color);
#else
	src = TTF_RenderText_Blended(font, text, color);
#endif
	SDL_BlitSurface(src, NULL, dst, &pos);
	SDL_FreeSurface(src);
}

void input_update(SDL_Surface *dst, int x, int y, char *text, int active, int flip)
{
	SDL_Rect pos;
	int xx, yy;
	char tmp;
	
#ifdef ZIPIT_Z2_UTF8 
	TTF_SizeUTF8(font, text, &xx, &yy);
#else
	TTF_SizeText(font, text, &xx, &yy);
#endif
	pos.x = x;
	pos.y = y;
	pos.w = WIDTH - x*2;
	pos.h = yy;
	SDL_FillRect(dst, &pos, BLACK);
	
	print(dst, x, y, text);
	/* display blinking cursor */
	if (flip / 5 % 2)
	{
		tmp = text[active];
		text[active] = '\0';
#ifdef ZIPIT_Z2_UTF8 
		TTF_SizeUTF8(font, text, &xx, &yy);
#else
		TTF_SizeText(font, text, &xx, &yy);
#endif
		print(dst, x+xx, y, "_");
		text[active] = tmp;
	}
	SDL_BlitSurface(dst, NULL, screen, NULL);
	SDL_Flip(screen);
}

#ifdef ZIPIT_Z2
/* dead key accent mappings for latin15 */
int acute_map[] = {
0xC1,'B','C','D',0xC9,'F','G','H',0xCD,'J','K','L','M','N',0xD3,'P','Q','R','S','T',0xDA,'V','W','X',0xDD,'Z',
'\'',0,0,0,0,0,
0xE1,'b','c','d',0xE9,'f','g','h',0xED,'j','k','l','m','n',0xF3,'p','q','r','s','t',0xFA,'v','w','x',0xFD,'z'};
int grave_map[] = {
0xC0,'B','C','D',0xC8,'F','G','H',0xCC,'J','K','L','M','N',0xD2,'P','Q','R','S','T',0xD9,'V','W','X','Y','Z',
'`',0,0,0,0,0,
0xE0,'b','c','d',0xE8,'f','g','h',0xEC,'j','k','l','m','n',0xF2,'p','q','r','s','t',0xF9,'v','w','x','y','z'};
int circumflex_map[] = {
0xC2,'B','C','D',0xCA,'F','G','H',0xCE,'J','K','L','M','N',0xD4,'P','Q','R','S','T',0xDB,'V','W','X','Y','Z',
'^',0,0,0,0,0,
0xE2,'b','c','d',0xEA,'f','g','h',0xEE,'j','k','l','m','n',0xF4,'p','q','r','s','t',0xFB,'v','w','x','y','z'};
int diaeresis_map[] = {
0xC4,'B','C','D',0xCB,'F','G','H',0xCF,'J','K','L','M','N',0xD6,'P','Q','R','S','T',0xDC,'V','W','X','Y','Z',
'\"',0,0,0,0,0,
0xE4,'b','c','d',0xEB,'f','g','h',0xEF,'j','k','l','m','n',0xF6,'p','q','r','s','t',0xFC,'v','w','x',0xFF,'z'};
int cedilla_map[] = {
'A','B',0xC7,'D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
',',0,0,0,0,0,
'a','b',0xE7,'d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z'};
int tilde_map[] = {
0xC3,'B','C','D','E','F','G','H','I','J','K','L','M',0xD1,0xD5,'P','Q','R','S','T','U','V','W','X','Y','Z',
'~',0,0,0,0,0,
0xE3,'b','c','d','e','f','g','h','i','j','k','l','m',0xF1,0xF5,'p','q','r','s','t','u','v','w','x','y','z'};

static int dead_key = 0;
#endif

/* input text */
void input(SDL_Surface *dst, int x, int y, char *text, int max)
{
	SDL_Event event;
	int action, active = 0, flip = 0;
	int up = 0, down = 0;
	
	#ifdef _PSP_FW_VERSION
	/* danzeff */
	if (config.danzeff)
	{
		strcpy(text, "");
		danzeff_load();
		danzeff_set_screen(dst);
		danzeff_moveTo(165, 110);
		
		for (;;)
		{
			SceCtrlData ctrl = getCtrlFromJoystick(joystick);
			int c = danzeff_readInput(ctrl);
			
			switch (c)
			{
				case 0:
					break;
				case '\10':
				case DANZEFF_LEFT:
					if (active > 0)
					{
						active--;
						text[active] = '\0';
					}
					break;
				case DANZEFF_RIGHT:
					if (active < max)
					{
						text[active] = ' ';
						active++;
						text[active] = '\0';
					}
					break;
				case DANZEFF_SELECT:
				case DANZEFF_START:
					return;
				default:
					if (active < max)
					{
						text[active] = c;
						active++;
						text[active] = '\0';
					}
					break;
			}
			
			danzeff_render();
			input_update(dst, x, y, text, active, flip);
			flip++;
			SDL_Delay(50);
			
			/* flush events */
			while (SDL_PollEvent(&event));
		}
	}
	else	
	#endif
	
	/* arcade */
	{
#ifdef ZIPIT_Z2
		SDL_EnableUNICODE(1);
#endif
		strcpy(text, " ");
		for (;;)
		{
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
					case SDL_QUIT:
						quit();
						break;
					case SDL_KEYDOWN:
					case SDL_JOYBUTTONDOWN:
						if (event.type == SDL_KEYDOWN)
							action = event.key.keysym.sym;
						else
							action = event.jbutton.button;
						switch (action)
						{
#ifdef ZIPIT_Z2
							case SDLK_ESCAPE:
							case SDLK_RETURN: 
								return;
							case SDLK_LEFT:
							case SDLK_BACKSPACE:
#ifdef ZIPIT_Z2_UTF8 
								/* Gotta backup more than 1 for UTF8 chars. */
 								if ((active > 0) && ((text[active] & 0xC0) == 8))
								{
									text[active] = '\0';
									active--;
								}
								if ((active > 0) && ((text[active] & 0xC0) == 8))
								{
									text[active] = '\0';
									active--;
								}
#endif /* ZIPIT_Z2_UTF8 */
								if (active > 0)
								{
									text[active] = '\0';
									active--;
								}
								else
									text[active] = ' ';
								break;
							case SDLK_RIGHT:
#else
							case SDLK_ESCAPE:
							case SDLK_SPACE:
							case PSP_BUTTON_START:
								return;
							case SDLK_LEFT:
							case PSP_BUTTON_LEFT:
							case PSP_BUTTON_L:
							case PSP_BUTTON_A:
								if (active > 0)
								{
									text[active] = '\0';
									active--;
								}
								break;
							case SDLK_RIGHT:
							case PSP_BUTTON_RIGHT:
							case PSP_BUTTON_R:
							case PSP_BUTTON_B:
							case PSP_BUTTON_Y:
							case PSP_BUTTON_X:
#endif
								if (active < max)
								{
									active++;
									text[active] = ' ';
									text[active+1] = '\0';
								}
								break;
							case SDLK_UP:
							case PSP_BUTTON_UP:
								up = 1;
								break;
							case SDLK_DOWN:
							case PSP_BUTTON_DOWN:
								down = 1;
								break;
							default:
#ifdef ZIPIT_Z2 /* ZIPIT_Z2_LATIN */
		      if (event.type == SDL_KEYDOWN)
		      {
			int key = action;  //event.key.keysym.sym;
			int fl = event.key.keysym.mod;

			if (((key >= 'a') && (key <= 'z')) || (key == ' '))
			{
			  if (fl & KMOD_SHIFT) 
			    key = toupper(key);

			  //if (!(fl & KMOD_CTRL) && (event.key.keysym.unicode == 0)) // Dead key
			  if (event.key.keysym.unicode == 0) // Dead key
			  {
			    switch (key) {
			    case 'A':
			    case 'G':
			    case 'D':
			    case 'F':
			    case 'T':
			    case 'C':
			      dead_key = key;
			    default:
			      break;
			    case 'V':
			      dead_key = 'C'; // (doppleganger for c)
			      break;
			    }
			    //if (dead_key) fprintf(fp, "  dead=%c\n", dead_key);
			    //else fprintf(fp, "  undead=0\n");
			    break;	/* skip COMPOSE (dead) keys */
			  }
			  else if (dead_key)
			  {
			    key = event.key.keysym.unicode;
			    if((key == 'V') && ((fl & KMOD_ALT) && (fl & KMOD_SHIFT)))
			    {
			      //fprintf(fp, "  decomposing %c+<%c>", dead_key, key);
			      break; // (doppleganger for c)
			    }
			    //fprintf(fp, "  composing %c+<%c>", dead_key, key);
			    if (key == ' ')
			      key = 'Z'+1;
			    if (dead_key == 'G') // (event.key.keysym.unicode == 0x300) // grave 
			      key = grave_map[key - 'A'];
			    else if (dead_key == 'A') // (event.key.keysym.unicode == 0x301) // acute 
			      key = acute_map[key - 'A'];
			    else if (dead_key == 'C') // (event.key.keysym.unicode == 0x302) // circum 
			      key = circumflex_map[key - 'A'];
			    else if (dead_key == 'T') // (event.key.keysym.unicode == 0x303) // tilde 
			      key = tilde_map[key - 'A'];
			    else if (dead_key == 'D') // (event.key.keysym.unicode == 0x308) // dia 
			      key = diaeresis_map[key - 'A'];
			    else if (dead_key == 'F') // (event.key.keysym.unicode == 0x327) // cedilla 
			      key = cedilla_map[key - 'A'];
			    dead_key = 0;
			    //fprintf(fp, " = +<%c>\n", key);
			  }
			  else if((key == 'P') && ((fl & KMOD_ALT) && (fl & KMOD_SHIFT)))
    			    break; // (doppleganger for o)
			  else if((key == 'Q') && ((fl & KMOD_ALT) && (fl & KMOD_SHIFT)))
    			    key = 0xBF; // Happier place for upside down question
			  else if((key == 'W') && ((fl & KMOD_ALT) && (fl & KMOD_SHIFT)))
    			    break; // (doppleganger for e)
			  else if((key == 'V') && ((fl & KMOD_ALT) && (fl & KMOD_SHIFT)))
			  {
			    dead_key = 'C';  // Happier place for circumflex
			    //fprintf(fp, "  fakeDEAD=%c\n", dead_key);
			    break;
			  }
			  else
			  {
			  dead_key = 0;
			  if (fl & KMOD_CTRL)
			  {
			    /* Skip unicode translation for ctrl keys. */
			  } 
			  key = event.key.keysym.unicode;
			  }
			event.key.keysym.unicode = key;
			}
		      }
#endif /* ZIPIT_Z2_LATIN */

#ifdef ZIPIT_Z2
								if ((event.type == SDL_KEYDOWN) && (event.key.keysym.unicode != 0))
						                {
#ifdef ZIPIT_Z2_UTF8 
								  /* 0xff00 would pass 8bit latin1, but ttf font wont.*/
						                  if ((event.key.keysym.unicode & 0xff80) == 0) // ASCII
						                  {
						                    text[active] = event.key.keysym.unicode;
						                    if (active < max)
						                      text[++active] = ' ';
						                    text[active+1] = '\0';
						                  }
								  else // UCS-2 Unicode.  Convert to UTF-8
						                  {
								    wchar_t wc = event.key.keysym.unicode;
								    char buf[4] = {0};
								    int len = 2;
								    if (wc < 0x800)
								    {
								      buf[0] = (0xC0 | wc>>6);
								      buf[1] = (0x80 | wc & 0x3F);
								    }
								    else
								    {
								      len=3;
								      buf[0] = (0xE0 | wc>>12);
								      buf[1] = (0x80 | wc>>6 & 0x3F);
								      buf[2] = (0x80 | wc & 0x3F);
								    }
								    if (active+len <= max)
								    text[active] = 0;
						                    strcat(text, buf);
								    active += len;
						                    if (active < max)
						                      text[++active] = ' ';
						                    text[active+1] = '\0';
						                  }
#else
								  /* 0xff00 will pass 8bit latin1, but will ttf font?*/
						                  if ((event.key.keysym.unicode & 0xff00) == 0) //Latin1
						                  {
						                    text[active] = event.key.keysym.unicode;
						                    if (active < max)
						                      text[++active] = ' ';
						                    text[active+1] = '\0';
						                  }
#endif
						                }
#endif
								break;
						}
						break;
					case SDL_KEYUP:
					case SDL_JOYBUTTONUP:
						if (event.type == SDL_KEYUP)
							action = event.key.keysym.sym;
						else
							action = event.jbutton.button;
						switch (action)
						{
							case SDLK_UP:
							case PSP_BUTTON_UP:
								up = 0;
								break;
							case SDLK_DOWN:
							case PSP_BUTTON_DOWN:
								down = 0;
								break;
						}
						break;
				}
			}
			if (up == 1 || up > 10)
			{
				if ((text[active] >= 'A' && text[active] < 'Z') || (text[active] >= '0' && text[active] < '9')) text[active]++;
				else if (text[active] == 'Z') text[active] = '0';
				else if (text[active] == '9') text[active] = ' ';
				else if (text[active] == ' ') text[active] = 'A';
			}
			if (up) up++;
			if (down == 1 || down > 10)
			{
				if ((text[active] > 'A' && text[active] <= 'Z') || (text[active] > '0' && text[active] <= '9')) text[active]--;
				else if (text[active] == 'A') text[active] = ' ';
				else if (text[active] == ' ') text[active] = '9';
				else if (text[active] == '0') text[active] = 'Z';
			}
			if (down) down++;
			input_update(dst, x, y, text, active, flip);
			flip++;
			SDL_Delay(50);
		}
	}
}
