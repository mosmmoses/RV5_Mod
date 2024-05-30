#include "ProjectMain.h"
#include "jr_usart_103_hal.h"
#include "HelperLib.h"

/* ---------------------------------------------------------- */
/*                            TIME                            */
/* ---------------------------------------------------------- */

__attribute__ ((optimize(0)))  void halt(void){					// indian style wait untill 1ms end
	static unsigned int temp  = 0;
	temp = TIM4->CNT;
//	while(TIM4->CNT != 0);
	while ( temp == TIM4->CNT );
	temp = TIM4->CNT;
	while ( temp == TIM4->CNT );
	dev.clock++;
}


void pause(int v){
	volatile int i;
	for (i = 0 ; i < v ; i++) halt();
}


/* ---------------------------------------------------------- */
/*                             ADC                            */
/* ---------------------------------------------------------- */
// Копирование в буфер свежих данных всех потов в столбец x
void Adc1Copy(int x){
	for (int y = 0 ; y < ADC_NUM_CHANNELS ; y++)
		Adc1Values[y][x] = Adc1ConvertedValue[y] >> 2;				// y столбец = номер пота, строка- содержимое
}

// сортировка столбца пузырьком
void AdcSort(int y){
	int x , e ;
	uint16_t v;
	do {
		e = 0;
		for (x = 0 ; x < (kNumMeasure-1) ; x++){
			if ( Adc1Values[y][x] < Adc1Values[y][x+1] ) {
				v = Adc1Values[y][x];
				Adc1Values[y][x] = Adc1Values[y][x+1];
				Adc1Values[y][x+1] = v;
				e++;
			}
		}
	} while (e);
}



// фильтр от шума потенциометров
// на выходе значения 0 .. 255
// f2 = (int) f1
void AdcFilter(void){
	int i;
	float v, v2;
	for (i = 0 ; i < ADC_NUM_CHANNELS ; i++) {
		v = pots[i].mediana ;
		if ( jquad(pots[i].f1 - v)  > 25) {									// если покрутили ручку, то фильтр грубеет на 100 итераций
			pots[i].iterator = 100;												// счетчик на 100
			pots[i].f1 = v;														// фильтр инициируется нефильтрованным средним
		}

																				// если все как обычно, то фильтр первого порядка с мелким блендом и второй фильтр покрупнее
																				// если крутнули ручку, то фильтр первого порядка с крупным блендом и только.
		if (pots[i].iterator == 0) 	{											// 0.02 - оптимально
			v =		( (v - pots[i].f1) * pots[i].cap) + pots[i].f1;
			v2 =	( (v - pots[i].f2) *  0.005f    ) + pots[i].f2;
			//pots[x].debug_flt = 0;
		}
		else {
			v =		( (v - pots[i].f1)  * 0.125f    ) + pots[i].f1;				// крупный конденсатор
			pots[i].iterator--;
			v2 = v;
		}

		if (v2 <= 4) v2 = 0;
		pots[i].f1 = v;
		pots[i].f2 = (int)v2;
	}
}


// сканирование ручек.  Сортировка, выборка средней порции
// триггеры и прочее - не здесь, здесь только мгновенные значения от 0 до 255
// ADC крутится в режиме "сам по себе" и данные копируются когда можем их скопировать.
void ScanKnobs(void){											// опрос потов.копирует вектор в массив, сортирует, берет среднюю порцию
	static int x = 0;											// номер строки массива, в которую сейчас копируются данные
	int i, j;													// счетчик цикла
	float v;													// value
	x++ ; x %= kNumMeasure;										// набор 16 значений АЦП. Номер столбца

	Adc1Copy(x);												// копирование потов в столбец
	if (!x ){													// сортировка ~ раз в 16 вызовов
		for (i = 0 ; i < ADC_NUM_CHANNELS ; i++)	{					// перебор потенциометров
			pots[i].f2_old = pots[i].f2;						//
			AdcSort(i); 										// сортировка строки пота

			v = 0;												// средняя порция
			for(j = 0 ; j < kNumMeasure/2 ; j++ ) v += Adc1Values[i][(kNumMeasure/4)+j];
			v /= 512.0f;										// приведение
			pots[i].mediana = v;								// -- значение для полировки на прерываниях и проверки в USR, когда прерываний - нет
		}
	}
	AdcFilter();												// фильтрация значений каждый вызов
}



// проверка одной ручки, установка триггера если были изменения
void ScanPotsShadowProc(int p , int i){
	float delta;
	delta = jquad(pots[i].f2 - pots[i].val_temp);				// квадрат разности
	if (delta >= 1*1) {											// порог срабатывания
		pots[i].onRotate[p] = kSet;
		pots[i].val_old[p] = pots[i].val[p] = pots[i].f2/1024.0;// пересчет значений
		pots[i].val_int[p] = pots[i].f2;
	}
}


// скан потенциометров.
void ScanPotsShadow(void) {
	int i;
	ScanKnobs();
	for (i = 0; i < ADC_NUM_CHANNELS  ; i++){
		ScanPotsShadowProc(0 , i);								// без зажатых кнопок
		pots[i].val_temp = pots[i].f2;							// запоминание положения ручки без зажатой кнопки, чтобы не реагировать на неё, когда кнопку зажмут
	}
}



// сброс всех триггеров перед новым опросом.
void TriggersResetAll(void){
	for (int i = 0; i < ADC_NUM_CHANNELS ; i++)
		pots[i].onRotate[0] = kReset;
}

// сброс триггеров нажатий кнопок
void KnobsResetAll(void) {
	keys[0].flag_release_short = kReset;
	keys[0].flag_hold_very_longer = kReset;
	keys[0].flag_release_very_longer = kReset;

	keys[1].flag_release_short = kReset;
	keys[1].flag_hold_very_longer = kReset;
	keys[1].flag_release_very_longer = kReset;
}

/* Инициализация потенциометров.
 * Пустое измерение чтобы установились выходные значения фильтров и не было ложных срабатываний
 */
void PotsInit(void){
	int  i;
	for (i=0 ; i < ADC_NUM_CHANNELS  ; i++) {
		pots[i].cap = 0.01f;									// начальная емкость конденсаторов
		pots[i].iterator = 900;
	}

	for (i=0 ; i < 2000  ; i++) {								// накачка заряда фильтров
		ScanPotsShadow();
	};
}



/* ---------------------------------------------------------- */
/*                           KEY                              */
/* ---------------------------------------------------------- */
void Key(KeyStruct * k){
	static unsigned int  s;
	s = k->gp->IDR & k->pin;

	if (k->drebezg_release >= 0){									// была недавно отпущена клавиша: выдерживается пауза для избежания дребезга
		k->drebezg_release--;
		k->state = key_bounce_release;
	}

	else {															// дребезга отпускания нет
		if ( k -> drebezg_counter == -1 ){							// не запущен счетчик, тогда происходит опрос кнопки на первичное нажатие
			if ( s == 0 ){
				k->drebezg_counter = 0;								// если  случилось первое нажатие, то инициализируем счетчик
				k->time = dev.clock;								// запоминается системное время первого нажатия
				k->state = key_first_press;
				k->flag_first_click = kSet;							// семафор (4)
			}
			else k->state = key_nothing;							// Первичного нажатия не было, кнопку не нажимали
		}
		else {														// счетчик дребезга уже запущен, первичное нажатие было
			k->drebezg_counter++;									// икремент

			if ( k->drebezg_counter >= DREBEZG ) {					// пропустили заданное количество тиков, опрос дальше
					if (s == 0) {									// опрос: кнопку еще держат?
						if (k->drebezg_counter >= LONG_PRESS){		// кнопку держат
							if (k->drebezg_counter > LONG_PRESS){
								k->state = key_hold_longer;			// событие длинного нажатия наступило и кнопку держат дальше. счетчик не сбрасываем
								k->flag_hold_longer = kSet;			// оно циклическое
							}
							else {	// ==
								k->state = key_hold_long;			// событие длинного нажатия = наступило. счетчик не сбрасывается
								k->flag_hold_long = kSet;			// семафор (20)
							}
							if (k->drebezg_counter > VERY_LONG_PRESS){// кнопку держат
								k->flag_hold_very_longer = kSet;	// также циклическое
							}
						}
						else k->state = key_hold_short;				// Кнопку еще держат, нажатие между коротким и длинным. Счетчик не сбрасывается
					}
					else {											// кнопку отпустили
						k->time_release = dev.clock;				// запоминается время отпускания
						k->drebezg_release = DREBEZG_RELEASE;		// инициализируется счетчик дребезга отпускания

						// сперва сравнивается длительность нажатия, потом останавливается счетчик дребезга нажатия
						if (k->drebezg_counter >= LONG_PRESS) {
							k->state = key_release_long;	 		// событие, что держали долго кнопку и в итоге отпустили
							k->flag_release_long = kSet;			// семафор (23)
						}
						else {
							k->state = key_release_short;			// кнопку держали недолго
							k->flag_release_short = kSet;			// семафор (1)
						}
						if (k->drebezg_counter > VERY_LONG_PRESS){
							k->flag_release_very_longer = kSet;
						}

						k->drebezg_counter = -1;					// счетчик дребезга нажатия останавливаем
					}
			}
			else k->state = key_bounce_press;						// пропуск тиков дребезга
		}
	}
}



/* ---------------------------------------------------------- */
/*                          Прочее                            */
/* ---------------------------------------------------------- */

// возведение в квадрат
float jquad(float d) {
	return(d*d);
}


// копирование побайтовое
void LDIR(char *HL , char *DE , int BC){						// откуда, куда, сколько
	do {
		*DE++ = *HL++;
	} while (BC--);
}

void LDIRc(const char *HL , char *DE , int BC) {				// откуда, куда, сколько
	do {
		*DE++ = *HL++;
	} while (BC--);
}

//очистка массива, memset()
void CLEARS( char *HL , int BC){									// что, сколько
	do {
		*HL++ = 0;
	}
	while (BC--);
}
