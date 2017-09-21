// constantes de las operaciones
#define DADDI 8
#define DADD 32
#define DSUB 34
#define LW 35
#define SW 43
#define BEQZ 4
#define BNEZ 5
#define FIN 63

#include <pthread.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <stdio.h>
#include <vector>
#include <iomanip>      // std::setw
#include <string.h>
#include <errno.h>


void inicioDeEjecucion();

pthread_barrier_t barrera;
std::vector<int> instrucciones;
std::vector<int> inicioHilos;
int cantidadDeHilosPorProcesar;
int * memoriaPrincipal = new int[96];
pthread_mutex_t mutex_hilos_procesar;

// prototipo de clase para poder crear un vector de procesadores que la misma clase ocupa
class Procesador;

/**
  *  Estados procesador:
  *  Aqui se guarda el estado de cada procesador al terminar un hilo
  **/
std::vector<Procesador> historialProcesador;
/**
  * Vector de procesadores 
  **/
std::vector<Procesador> procesador;

// clase que contiene lo esencial que simulara el procesador
// se realiza la implementacion en el mismo archivo para evitar complicaciones con la compilacion
//sobre todo por el uso necesario de variables globales requeridas para poder trabajar con hilos
class Procesador{
private:
	// define la forma en que se interpretara el puntero del vector del instrucciones
	struct Instruccion{
		int codigo;
		int p1;
		int p2;
		int p3;
	};
	// los registros del procesador
	int Reg[32];
	// ciclo al empezar
	int cicloAlEmpezarElHilo;
	// nombre del procesador
	std::string nombre;
	// nombre del procesador
	int numeroProcesador;

	// operador copiar de memoria en cache
	// copia los datos de un bloque de memoria en un cache, obtiene el numero de bloque de la cache por mapeo directo aplicando modulo
	void copiarDeMemoriaEnCache(int numeroBloqueMemoria){
		for(int i = 0; i<4; ++i)
		{
			cache[numeroBloqueMemoria%4].palabra[i] = memoriaPorBloque[numeroBloqueMemoria].palabra[i];
		}
		cache[numeroBloqueMemoria%4].estado = 'c';
		cache[numeroBloqueMemoria%4].etiqueta = numeroBloqueMemoria;
	}

public:
	// operador copiar en memoria desde cache
	// copia el contenido de la cache en el bloque de memoria que le pertenece, que se encuentra en etiqueta
	void copiarDeCacheEnLaMemoria(int numeroBloqueCache){
		for(int i = 0; i<4; ++i)
		{
			memoriaPorBloque[ cache[numeroBloqueCache].etiqueta ].palabra[i] = cache[numeroBloqueCache].palabra[i];
		}
	}
	// contador interno de ciclos del hilo
	int ciclos;
	// nombre que identifica al hilo que esta corriendo
	std::string idHilo;
	// se usa para ver la memoria por bloques
	struct Bloque{
		int palabra[4];
	};
	Bloque* memoriaPorBloque;
	// estructura de un bloque de cache
	struct BloqueCache{
		// cada bloque de cache tiene capacidad para cuatro palabras
		int palabra[4];
		// numero del bloque de memoria cargado
		int etiqueta;
		// estado en que se encuentra el bloque
		char estado;
		// todos los bloques empiezan como invalidos
		BloqueCache() : etiqueta(-1), estado('i'){ };
	};

	struct PosicionDirectorio{
		char estado;
		bool procesadores[3];

		PosicionDirectorio() : estado('u'){
			for(int i = 0; i < 3; ++i)
				procesadores[i] = false;
		}
	};
	// cada procesador tiene 4 bloques de cache
	BloqueCache cache[4];
	// cada procesador tiene un directorio para los 8 bloques de memoria
	PosicionDirectorio directorio[8];
	// contador de programa
	int PC;
	// estdao actual del procesador (trabajando, esperando por asignacion)
	char estado;
	// se utilizara como apuntador a la instruccion
	Instruccion *instruccionActual;
	// indica si esta corriendo
	bool corriendo;
	pthread_mutex_t mutex_cache;
	pthread_mutex_t mutex_directorio;

private:
	// ocurre un ciclo de reloj
	void tick(){
		pthread_barrier_wait(&barrera);
		pthread_barrier_wait(&barrera);
	}
	// se indica cuantos ciclos de reloj debe esperar
	void ticks(int n){
		for (int i = 0; i < n; ++i){
			tick();
		}
	}

public:
	// contructor del procesador
	// el nombre por defecto del procesador va a ser "ProcesadorX"
	Procesador(int numero, std::string nombre = "ProcesadorX"):
		estado('e'), // estado va a ser de inicio esperando
		corriendo(true),  //por defecto va a estar corriendo cuando se construye
		ciclos(0), // contador de ciclos en 0
		nombre(nombre),
		numeroProcesador(numero)
	{
		printf("Creado el procesador: %s\n", nombre.c_str());
		memoriaPorBloque = (Bloque*) memoriaPrincipal;
		for(int  i = 0; i < 32; ++i){
			Reg[i] = 0;
		}
		//printf("procesador: %s | dir_nombre: %p | dir_mut_cac: %p | dir_mut_dir: %p\n", nombre.c_str(), &nombre, &mutex_cache, &mutex_directorio );
		pthread_mutex_init(&mutex_cache, NULL);
		pthread_mutex_init(&mutex_directorio, NULL);
	}

	void asignar(int PCNuevo, std::string nombreHilo,int ciclo){ // se asigna un nuevo Program Counter
		PC = PCNuevo;
		ciclos = 0; // reinicia el contador de ciclos
		estado = 't'; // pasa su estado a trabajando
		idHilo = nombreHilo;
		cicloAlEmpezarElHilo = ciclo;
		// indica que el procesador X va a procesar Y hilo
		std::cout << "El procesador: " << nombre << " va a procesar el hilo " << idHilo << std::endl;
	}

	/*
	* Imprime los datos solicitados (registros, cache, ciclos)
	*/
	void imprimirEstado() const
	{
		std::cout << "Procesador: " << nombre << std::endl;
		std::cout << "Nombre del hilo: " << idHilo << std::endl;
		std::cout << "Tardo: " << ciclos << " ciclos de reloj" << std::endl;
		std::cout << "Cuando comenzo el valor del reloj era: " << cicloAlEmpezarElHilo << '\n' << std::endl;
		std::cout << "Contenido de Registros:" << std::endl;
		for(int i = 0; i < 32; ++i){
			std::cout << "R" << std::setfill('0') << std::setw(2) << i << ": " << std::setfill(' ') << std::setw(4) << Reg[i] << " | ";
			if(i  && ! ((i+1)%8) )
				std::cout << std::endl;
		}

		std::cout << "\n\nDirectorio" << std::endl;
		for(int i = 0; i < 8; ++i){
			std::cout << "Posicion " << i << std::endl;
			std::cout << "estado: " << directorio[i].estado << std::endl;
			for(int j = 0; j < 3; ++j){
				std::cout << "Proc" << j << " : " << directorio[i].procesadores[j] << " | ";
			}
			std::cout << std::endl;
		}
		std::cout << "\n\nContenido de Cache" << std::endl;
		for(int i = 0; i < 4; ++i){
			std::cout << "Posicion " << i << std::endl;
			std::cout << "estado: " << cache[i].estado << " | bloque cargado: " << cache[i].etiqueta << std::endl;
			for(int j = 0; j < 4; ++j){
				std::cout << "palabra " << j << " : " << std::setw(4) << cache[i].palabra[j] << " | ";
			}
			std::cout << std::endl;
		}
	}

	// imprime lo que se encuentre en memoria, y el estado de todos lo hilos del historial que guarda
	void imprimirEstadosDelHistorial(){
		std::cout << "\n-------------------------\n" << std::endl;
		std::cout << "Memoria Principal" << std::endl;
		for(int p=0; p < 3; ++p)
		{
			std::cout << "Memoria Compartida en P" << p << std::endl;
			for(int i=0; i<32;++i)
			{
				std::cout << std::setw(3) << (32*p+i)*4 << ": " << std::setw(4) << memoriaPrincipal[32*p+i] << " | ";
				if( i && ! ((i+1)%4) )
					std::cout << std::endl;
			}
			std::cout << '\n' << std::endl;
		}
		std::cout << "\n-------------------------\n" << std::endl;
		for( std::vector<Procesador>::const_iterator i = historialProcesador.begin(); i != historialProcesador.end(); ++i){
			i->imprimirEstado();
			std::cout << "\n-------------------------\n" << std::endl;
		}
	}
	// metodo principal que realiza la simulacion
	void correr(){
		int posicionDeMemoria;
		int numeroBloqueDeMemoria;
		int numeroBloqueDeMemoriaEnCache;
		int procesadorAlQuePerteneceDireccionMemoria;
		int procesadorAlQuePerteneceMemoriaDelBloqueEnCache;
		int procesadorEnQueSeEncuentraModificado;
		int numeroProcesadoresEnQueSeEncuentraCompartido;
		int procesadorEnQueSeEncuentraCompartido[2];
		bool error;
		int numError;
		while(corriendo){
			pthread_barrier_wait(&barrera);
			//printf("\t%s: estado -> %c\n",nombre.c_str(),estado);
			if(estado == 't')
			{
				//printf("%s: -- En Uso PC: %i => Codigo Instruccion: %i\n",nombre.c_str(),PC,instrucciones[PC]);
				// revisa el codigo de la instruccion actual
				instruccionActual = (Instruccion*)&instrucciones[PC];
				// aumenta el contador de programa justo al leer la instruccion
				PC += 4;
				switch (  instruccionActual->codigo )
				{
					case DADDI: // realiza una suma de un inmediato con un registro
						if( instruccionActual->p2 ){
							//printf("\t%s: ejecutando un DADDI. R%i = R%i[%i] + %i\n",nombre.c_str(),instruccionActual->p2,instruccionActual->p1,Reg[instruccionActual->p1],instruccionActual->p3);
							Reg[instruccionActual->p2] = Reg[instruccionActual->p1] + instruccionActual->p3;
						}
					break;
					case DADD: // suma de dos registros
						if( instruccionActual->p3 ){
							//printf("\t%s: ejecutando un DADD. R%i = R%i[%i] + R%i[%i]\n",nombre.c_str(),instruccionActual->p3,instruccionActual->p1,Reg[instruccionActual->p1],instruccionActual->p2,Reg[instruccionActual->p2]);
							Reg[instruccionActual->p3] = Reg[instruccionActual->p1] + Reg[instruccionActual->p2];
						}
					break;
					case DSUB: // resta de dos registros
						if( instruccionActual->p3 ){
							//printf("\t%s: ejecutando un DSUB. R%i = R%i[%i] - R%i[%i]\n",nombre.c_str(),instruccionActual->p3,instruccionActual->p1,Reg[instruccionActual->p1],instruccionActual->p2,Reg[instruccionActual->p2]);
							Reg[instruccionActual->p3] = Reg[instruccionActual->p1] - Reg[instruccionActual->p2];
						}
					break;
					case BEQZ: // branch, realiza un salto si el registro es igual a creo
						//printf("\t%s: ejecutando un BEQZ. if( R%i[%i] = 0 ) PC +=  %i\n",nombre.c_str(),instruccionActual->p1,Reg[instruccionActual->p1],instruccionActual->p3);
						if( ! Reg[instruccionActual->p1] ) // entra si es igual a cero
						{
							// le suma el valor indicado multiplicado por 4 ( <<2 = *4 )
							PC += (instruccionActual->p3)<<2;
						}
					break;
					case BNEZ: // branch, realiza un salto si el registro es diferente de creo
						//printf("\t%s: ejecutando un BNEZ. if( R%i[%i] != 0 ) PC +=  %i\n",nombre.c_str(),instruccionActual->p1,Reg[instruccionActual->p1],instruccionActual->p3);
						if ( Reg[instruccionActual->p1] ) // entra si no es cero
						{
							// le suma el valor indicado multiplicado por 4 ( <<2 = *4 )
							PC += (instruccionActual->p3)<<2;
						}
					break;
					case FIN: // finaliza el hilo

						// el procesador pasa a estar esperando
						estado = 'e';

						pthread_mutex_lock(&mutex_hilos_procesar);
						// guarda los datos al terminar de procesar el hilo
						historialProcesador.push_back(*this);

						// indica que falta un hilo menos
						--cantidadDeHilosPorProcesar;
						
						pthread_mutex_unlock(&mutex_hilos_procesar);
						//printf("\t%s: llegue al fin\n",nombre.c_str());
					break;
					case LW:

						error = false;

						// obtiene la posicion solicitada
						posicionDeMemoria = Reg[instruccionActual->p1] + instruccionActual->p3;
						//obtiene el numero de bloque dividiendo entre 16
						numeroBloqueDeMemoria = posicionDeMemoria >> 4;


						//printf("\t%s: ejecutando un LW. Voy a leer la posicionDeMemoria: %i (bloque: %i)\n",nombre.c_str(),posicionDeMemoria,numeroBloqueDeMemoria);

						if( pthread_mutex_trylock(&mutex_cache) == 0 ) // pide el control de su cache
						{

							//printf("\t%s: obtuve mi cache\n",nombre.c_str());

							// ya tengo mi cache
							tick(); // la utilizo hasta el otro ciclo

							if(
								!( cache[numeroBloqueDeMemoria%4].etiqueta == numeroBloqueDeMemoria &&
								  (cache[numeroBloqueDeMemoria%4].estado == 'c' || cache[numeroBloqueDeMemoria%4].estado == 'm' ) )
							) // pregunta si el bloque requerido esta valido en cache
							{
								//el bloque no esta en la cache
								//printf("\t%s: fallo de cache. En la posicion de cache: %i, esta el bloque: %i y su estado es: %c\n",nombre.c_str(), numeroBloqueDeMemoria%4, cache[numeroBloqueDeMemoria%4].etiqueta, cache[numeroBloqueDeMemoria%4].estado );
								procesadorAlQuePerteneceDireccionMemoria = numeroBloqueDeMemoria/8;

								procesadorAlQuePerteneceMemoriaDelBloqueEnCache = cache[numeroBloqueDeMemoria%4].etiqueta/8;
								numeroBloqueDeMemoriaEnCache = cache[numeroBloqueDeMemoria%4].etiqueta;

								if( cache[numeroBloqueDeMemoria%4].estado == 'm' ) // pregunta si el bloque que voy a reemplazar esta modificado
								{
									//printf("\t%s: fallo de cache  y el bloque a reemplazar esta m\n",nombre.c_str());
									if( pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) ) == 0  ) // solicita el directorio en el cual se encuentra el bloque de la memoria que esta en cache
									{
										//printf("\t%s: obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);
										
										// obtuve el directorio
										tick(); //lo utilizo hasta el otro ciclo

										// copia a memoria el bloque que va a reemplazar
										copiarDeCacheEnLaMemoria( numeroBloqueDeMemoria%4 );

										// retraso que se toma al ir hasta memoria
										if(procesadorAlQuePerteneceMemoriaDelBloqueEnCache == numeroProcesador)
										{ //memoria local
											ticks(16);
										}
										else{ //memoria remota
											ticks(32);
										}

										// el bloque pasa a estar uncached 
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].estado = 'u';

										// indica al directorio que el bloque ya no esta en este procesador
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[numeroProcesador] = false;

										// libera el directorio
										pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) );
										//printf("\t%s: libere el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);


										// coloco la posicion de cache invalida
										cache[numeroBloqueDeMemoria%4].estado = 'i';
										tick();
										//printf("\t%s: AHORA En la posicion de cache: %i, esta el bloque: %i y su estado es: %c\n",nombre.c_str(), numeroBloqueDeMemoria%4, cache[numeroBloqueDeMemoria%4].etiqueta, cache[numeroBloqueDeMemoria%4].estado );


									}
									else{
										// no obtuve el directorio
										//printf("\t%s: NO obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										// libero mi cache
										pthread_mutex_unlock( &mutex_cache );

										//printf("\t%s: libere mi cache\n",nombre.c_str());

										// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
										PC -= 4;
										break;
									}
								}

								else if( cache[numeroBloqueDeMemoria%4].estado == 'c' ) // pregunta si el bloque que voy a reemplazar esta compartido
								{

									//printf("\t%s: fallo de cache  y el bloque a reemplazar esta s\n",nombre.c_str());

									// solicito el directorio
									if( pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) ) == 0 ){
										// obtuve el directorio
										tick(); //lo utilizo hasta el otro ciclo

										//printf("\t%s: obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										// indica al directorio que el bloque ya no esta en este procesador
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[numeroProcesador] = false;

										// Verifica en el directorio si el bloque si se encontraba c en algun otro procesador
										if( !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[0] && !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[1] && !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[2]){
											// el bloque pasa a estar uncached 
											procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].estado = 'u';
										}

										tick(); // uso del directorio

										// libera el directorio
										pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) );
										//printf("\t%s: libere el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										// coloco la posicion de cache invalida
										cache[numeroBloqueDeMemoria%4].estado = 'i';
										tick();
									}
									else{
										// no obtuve el directorio
										//printf("\t%s: NO obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										// libero mi cache
										pthread_mutex_unlock( &mutex_cache );

										//printf("\t%s: libere mi cache\n",nombre.c_str());
										// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
										PC -= 4;
										break;
									}
								}

								// ya puedo copiar el bloque de memoria en la posicion de cache

								if( pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) ) == 0 )  // solicito el directorio donde esta la memoria que necesito leer
								{
									//printf("\t%s: obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceDireccionMemoria);
									tick(); //lo utilizo hasta el otro ciclo

									switch ( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado )
									{

										case 'u':
										case 'c': // revisa si esta U o C
											// lo coloca como C por si acaso estaba U
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'c'; 

											// indica que este procesador va a tener cargado el bloque de memoria
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true; 

											// trae de memoria el bloque solicitado
											copiarDeMemoriaEnCache(numeroBloqueDeMemoria);
											if( procesadorAlQuePerteneceDireccionMemoria == numeroProcesador ){
												ticks(16);
											}
											else
											{
												ticks(32);
											}

										break;
										case 'm':

											// busca en cual procesador se encuentra modificado
											for(int i = 0; i < 3; ++i)
											{
												if( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[i] )
												{
													procesadorEnQueSeEncuentraModificado = i;
												}
											}

											// Solicito la cache del procesador en el cual se encuantra modificado (si se encuentra modificado no es en este procesador)
											if( pthread_mutex_trylock( &procesador[procesadorEnQueSeEncuentraModificado].mutex_cache ) == 0 )
											{
												tick(); // la obtuve. la utilizo en el otro ciclo

												// copio del procesador en el cual se encuentra el bloque modificado en la memoria
												procesador[procesadorEnQueSeEncuentraModificado].copiarDeCacheEnLaMemoria(numeroBloqueDeMemoria%4);

												// indico que en ese procesador ahora estara como compartido
												procesador[procesadorEnQueSeEncuentraModificado].cache[numeroBloqueDeMemoria%4].estado = 'c';

												// trae de memoria el bloque solicitado
												copiarDeMemoriaEnCache(numeroBloqueDeMemoria);

												// los ciclos que tarda en escribir en memoria
												if( procesadorEnQueSeEncuentraModificado == procesadorAlQuePerteneceDireccionMemoria ){
													ticks(16);
												}
												else
												{
													ticks(32);
												}

												// libero la cache del procesador en el cual estaba modficado
												pthread_mutex_unlock( &procesador[procesadorEnQueSeEncuentraModificado].mutex_cache );

												// ahora en el directorio va a estar compartido
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'c';

												// indica que este procesador va a tener cargado el bloque de memoria
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true;
											}
											else
											{
												error = true;
											}
										break;
									}
									// libera el directorio
									pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
									//printf("\t%s: libero el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceDireccionMemoria);
									if(error) // hubo un error libero todo
									{
										// libero mi cache
										pthread_mutex_unlock( &mutex_cache );

										//printf("\t%s: libere mi cache\n",nombre.c_str());

										// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
										PC -= 4;
										break;
									}
								}
								else
								{
									//printf("\t%s: NO obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceDireccionMemoria);
									

									// libero mi cache
									pthread_mutex_unlock( &mutex_cache );

									//printf("\t%s: libere mi cache\n",nombre.c_str());

									// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
									PC -= 4;
									break;
								}


							}
							//Cuando llega aqui ya tiene el bloque en cache
							// copia en la cache lo que se encuentra en el registro modificado
							Reg[instruccionActual->p2] = cache[numeroBloqueDeMemoria%4].palabra[(posicionDeMemoria>>2)%4];
							// libero mi cache
							pthread_mutex_unlock(&mutex_cache);
							//printf("\t%s: libere mi cache\n",nombre.c_str());

						}
						else
						{

							// no obtuve mi cache no puedo hacer nada :(
							//printf("\t%s: NO obtuve mi cache\n",nombre.c_str());

							// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
							PC -= 4;
						}
					break;
					case SW:
						//printf("\t%s: ejecutando un SW\n",nombre.c_str());
						// obtiene la posicion solicitada
						posicionDeMemoria = Reg[instruccionActual->p1] + instruccionActual->p3;
						//obtiene el numero de bloque dividiendo entre 16
						numeroBloqueDeMemoria = posicionDeMemoria >> 4;
						procesadorAlQuePerteneceDireccionMemoria = numeroBloqueDeMemoria/8;

						if( pthread_mutex_trylock(&mutex_cache) == 0 ) // pide el control de su cache
						{

							//printf("\t%s: obtuve mi cache\n",nombre.c_str());

							// ya tengo mi cache
							tick(); // la utilizo hasta el otro ciclo

							if(
								!( cache[numeroBloqueDeMemoria%4].etiqueta == numeroBloqueDeMemoria &&
								  (cache[numeroBloqueDeMemoria%4].estado == 'c' || cache[numeroBloqueDeMemoria%4].estado == 'm' ) )
							) // pregunta si el bloque requerido no esta en la cache o si esta invalido
							{

								//printf("\t%s: fallo de cache\n",nombre.c_str());
								
								//el bloque no esta en la cache
								procesadorAlQuePerteneceMemoriaDelBloqueEnCache = cache[numeroBloqueDeMemoria%4].etiqueta/8;
								numeroBloqueDeMemoriaEnCache = cache[numeroBloqueDeMemoria%4].etiqueta;

								if( cache[numeroBloqueDeMemoria%4].estado == 'm' ) // el bloque en cache esta modificado
								{
									//printf("\t%s: fallo de cache  y el bloque a reemplazar esta m\n",nombre.c_str());
									//printf("\t%s: Necesito el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

									if( pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) ) == 0 ) // solicita el directorio en el cual se encuentra la memoria que esta en cache
									{

										//printf("\t%s: obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										tick(); //lo utilizo hasta el otro ciclo

										// si el bloque que va a reemplazar esta modificado lo copia a memoria
										copiarDeCacheEnLaMemoria( numeroBloqueDeMemoria%4 );

										// retraso que se toma al ir hasta memoria
										if(procesadorAlQuePerteneceMemoriaDelBloqueEnCache == numeroProcesador)
										{ //memoria local
											ticks(16);
										}
										else{ //memoria remota
											ticks(32);
										}

										// el bloque pasa a estar uncached 
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].estado = 'u';

										// indica al directorio que el bloque ya no esta en este procesador
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[numeroProcesador] = false;

										// coloco la posicion de cache invalida
										cache[numeroBloqueDeMemoria%4].estado = 'i';
										tick();
										// libera el directorio
										pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) );
										//printf("\t%s: libere el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);
									}
									else{
										//printf("\t%s: NO obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);
										
										// libero mi cache
										pthread_mutex_unlock(&mutex_cache);

										//printf("\t%s: libere mi cache\n",nombre.c_str());

										// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
										PC -= 4;
										break;
									}
								}
								else if( cache[numeroBloqueDeMemoria%4].estado == 'c' ) // el bloque en cache esta compartido
								{
									//printf("\t%s: fallo de cache  y el bloque a reemplazar esta s\n",nombre.c_str());
									//printf("\t%s: Necesito el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

									if( pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) ) == 0 )
									{
										//printf("\t%s: obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);
										tick(); //lo utilizo hasta el otro ciclo

										// indica al directorio que el bloque ya no esta en este procesador
										procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[numeroProcesador] = false;

										// Verifica en el directorio si el bloque si se encontraba c en algun otro procesador
										if( !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[0] && !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[1] && !procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].procesadores[2]){
											// el bloque pasa a estar uncached 
											procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].directorio[numeroBloqueDeMemoriaEnCache%8].estado = 'u';
										}

										tick(); // usando el directorio

										// coloco la posicion de cache invalida
										cache[numeroBloqueDeMemoria%4].estado = 'i';

										// libera el directorio
										pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceMemoriaDelBloqueEnCache].mutex_directorio) );
										//printf("\t%s: libere el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);
									}
									else{
										//printf("\t%s: NO obtuve el directorio: %i\n",nombre.c_str(),procesadorAlQuePerteneceMemoriaDelBloqueEnCache);

										// libero mi cache
										pthread_mutex_unlock(&mutex_cache);


										//printf("\t%s: libere mi cache\n",nombre.c_str());

										// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
										PC -= 4;
										break;
									}
								}
								// ya puedo copiar el bloque de memoria en la posicion de cache
								//printf("\t%s: fallo de cache, voy a pedir el directorio que ocupo que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);

								if( (numError = pthread_mutex_trylock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) ) ) == 0 ) // solicito el directorio que contiene el bloque de memoria que ocupo
								{
									tick(); //lo obtuve y lo utilizo hasta el otro ciclo
									//printf("\t%s: obtuve directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);

									if ( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado == 'u') //esta uncached
									{
											//printf("\t%s: la posicion %i del directorio que pertenece a: %i estaba U\n",nombre.c_str(), numeroBloqueDeMemoria%8,procesadorAlQuePerteneceDireccionMemoria);

											// indica que ahora va a estar modificado
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'm';

											// indica que este procesador va a tener cargado el bloque de memoria
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true;


											tick();
											// trae de memoria el bloque solicitado
											copiarDeMemoriaEnCache(numeroBloqueDeMemoria);
											// retraso que se toma al ir hasta memoria
											if(procesadorAlQuePerteneceDireccionMemoria == numeroProcesador)
											{ //memoria local
												ticks(16);
											}
											else{ //memoria remota
												ticks(32);
											}

											// libera el directorio
											pthread_mutex_unlock( & (procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio ) );
											//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);

											cache[ numeroBloqueDeMemoria%4 ].estado = 'm';


									} // fin caso u

									else if ( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado == 'c')
									{
											//printf("\t%s: la posicion %i del directorio que pertenece a: %i estaba C\n",nombre.c_str(), numeroBloqueDeMemoria%8,procesadorAlQuePerteneceDireccionMemoria);
											numeroProcesadoresEnQueSeEncuentraCompartido = 0;

											// busca en cuales procesadores se encuentra compartido
											for(int i = 0; i < 3; ++i){
												if( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[i] ){
													procesadorEnQueSeEncuentraCompartido[numeroProcesadoresEnQueSeEncuentraCompartido] = i;
													++numeroProcesadoresEnQueSeEncuentraCompartido;
												}
											}

											if(numeroProcesadoresEnQueSeEncuentraCompartido == 1) // solo esta en un procesador
											{
												if( pthread_mutex_trylock( &(procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache) ) == 0 )
												{
													tick();
													// coloco la posicion de cache invalida
													procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
													pthread_mutex_unlock( &(procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache) );
													// indica al directorio que el bloque ya no esta en este procesador
													procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[procesadorEnQueSeEncuentraCompartido[0]] = false;
												}
												else{
													// libera el directorio
													pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
													//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);
													
													// libero mi cache
													pthread_mutex_unlock( &mutex_cache );

													//printf("\t%s: libere mi cache\n",nombre.c_str());

													// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
													PC -= 4;
													break;
												}
											}

											else if(numeroProcesadoresEnQueSeEncuentraCompartido == 2) // esta en dos caches
											{

												if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache ) == 0 ) // pide una
												{
													tick();
													// coloco la posicion de cache invalida
													procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
													pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache );
													// indica al directorio que el bloque ya no esta en este procesador
													procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[0] ] = false;
													if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache ) == 0 ) // obtuvo la primera y solicita la otra
													{
														tick();
														// coloco la posicion de cache invalida
														procesador[ procesadorEnQueSeEncuentraCompartido[1] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
														pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache );
														// indica al directorio que el bloque ya no esta en este procesador
														procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[ numeroBloqueDeMemoria%8].procesadores[procesadorEnQueSeEncuentraCompartido[1] ] = false;
													}
													else
													{
														// libera el directorio
														pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
														
														//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);
														
														// libero mi cache
														pthread_mutex_unlock( &mutex_cache );
														
														//printf("\t%s: libere mi cache\n",nombre.c_str());

														// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
														PC -= 4;
														break;
													}
												}
												else if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache ) == 0 ) // no le dieron la primera entonces pide la otra
												{
													tick();
													// coloco la posicion de cache invalida
													procesador[ procesadorEnQueSeEncuentraCompartido[1] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
													tick();
													pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache );
													// indica al directorio que el bloque ya no esta en este procesador
													procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[1] ] = false;
													if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache ) == 0 ) // solicita la que no le dieron la primera vez
													{
														tick();
														// coloco la posicion de cache invalida
														procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
														tick();
														pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache );
														// indica al directorio que el bloque ya no esta en este procesador
														procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[0] ] = false;
													}
													else
													{
														// libera el directorio
														pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
														
														//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);
														
														// libero mi cache
														pthread_mutex_unlock( &mutex_cache );

														//printf("\t%s: libere mi cache\n",nombre.c_str());

														// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
														PC -= 4;
														break;
													}
												}
												else // no obtuvo ninguna, entonces libera todo
												{
													// libera el directorio
													pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
													
													//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);
													
													// libero mi cache
													pthread_mutex_unlock( &mutex_cache );

													//printf("\t%s: libere mi cache\n",nombre.c_str());

													// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
													PC -= 4;
													break;
												}

											}



											// indica que ahora va a estar modificado
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'm';

											// indica que este procesador va a tener cargado el bloque de memoria
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true;



											//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);

											// trae de memoria el bloque solicitado
											copiarDeMemoriaEnCache(numeroBloqueDeMemoria);
											// retraso que se toma al ir hasta memoria
											if(procesadorAlQuePerteneceDireccionMemoria == numeroProcesador)
											{ //memoria local
												ticks(16);
											}
											else{ //memoria remota
												ticks(32);
											}

											// libera el directorio
											pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );

											cache[ numeroBloqueDeMemoria%4 ].estado = 'm';
									} // fin caso c
									else if ( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado == 'm')// esta m
									{
											//printf("\t%s: la posicion %i del directorio que pertenece a: %i estaba M\n",nombre.c_str(), numeroBloqueDeMemoria%8,procesadorAlQuePerteneceDireccionMemoria);
											
											// busca en cual procesador se encuentra modificado
											for(int i = 0; i < 3; ++i){
												if( procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[i] ){
													procesadorEnQueSeEncuentraModificado = i;
												}
											}
											// Solicito la cache del procesador en el cual se encuantra modificado (si se encuentra modificado no es en este procesador
											if( pthread_mutex_trylock( &procesador[procesadorEnQueSeEncuentraModificado].mutex_cache ) == 0 ) // solicito la cache del procesador en el cual se encuentra modificado
											{
												tick();

												procesador[procesadorEnQueSeEncuentraModificado].copiarDeCacheEnLaMemoria(numeroBloqueDeMemoria%4);

												// trae de memoria el bloque solicitado
												copiarDeMemoriaEnCache(numeroBloqueDeMemoria);

												// coloco la posicion de cache invalida del procesador en el cual se encontraba modificado
												procesador[procesadorEnQueSeEncuentraModificado].cache[numeroBloqueDeMemoria%4].estado = 'i'; // en este caso lo invalida porque va a modificarlo 
												tick();

												if( procesadorEnQueSeEncuentraModificado == procesadorAlQuePerteneceDireccionMemoria ){
													ticks(16);
												}
												else
												{
													ticks(32);
												}
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'm';

												// indica que este procesador va a tener cargado el bloque de memoria
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true;
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[procesadorEnQueSeEncuentraModificado] = false;
												pthread_mutex_unlock( &procesador[procesadorEnQueSeEncuentraModificado].mutex_cache );
												// libera el directorio
												pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
												//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);
											}
											else
											{ // no lo obtuve, libero todo
												// libera el directorio
												pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
												
												//printf("\t%s: libere el directorio que ocupaba, que pertenece a: %i\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria);


												// libero mi cache
												pthread_mutex_unlock( &mutex_cache );
												
												//printf("\t%s: libere mi cache\n",nombre.c_str());

												// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
												PC -= 4;
												break;
											}
									} // fin caso m
								}
								else
								{
									//printf("\t%s: NO obtuve directorio que ocupaba, que pertenece a: %i, porque: %s\n",nombre.c_str(), procesadorAlQuePerteneceDireccionMemoria, strerror(numError) );
									
									// libero mi cache
									pthread_mutex_unlock( &mutex_cache );
									//printf("\t%s: libere mi cache\n",nombre.c_str());

									// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
									PC -= 4;
									break;
								}


							}
							else if( cache[numeroBloqueDeMemoria%4].estado == 'c' ) //hit de memoria pero esta compartido
							{ 

								if( pthread_mutex_trylock( &procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio ) == 0 ) // obtengo el directorio donde se encuentra el bloque que voy a escribir
								{

									tick();
									numeroProcesadoresEnQueSeEncuentraCompartido = 0;

									/*
										Al igual que si no se encontrara cargado, debe buscar en cuales procesadores se encuentra compartido e invalidarlos 
									*/

									// busca en cuales procesadores se encuentra compartido
									for(int i = 0; i < 3; ++i){
										if( i != numeroProcesador && procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[i] ){
											procesadorEnQueSeEncuentraCompartido[numeroProcesadoresEnQueSeEncuentraCompartido] = i;
											++numeroProcesadoresEnQueSeEncuentraCompartido;
										}
									}

									if(numeroProcesadoresEnQueSeEncuentraCompartido == 1)
									{
										if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache ) == 0 )
										{
											tick();
											// coloco la posicion de cache invalida
											procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
											pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache );
											// indica al directorio que el bloque ya no esta en este procesador
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[0] ] = false;
										}
										else{
											// libera el directorio
											pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );

											// libero mi cache
											pthread_mutex_unlock( &mutex_cache );
											//printf("\t%s: libere mi cache\n",nombre.c_str());

											// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
											PC -= 4;
											break;
										}
									}

									else if(numeroProcesadoresEnQueSeEncuentraCompartido == 2)
									{

										if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache ) == 0 )
										{
											tick();
											// coloco la posicion de cache invalida
											procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
											pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache );
											// indica al directorio que el bloque ya no esta en este procesador
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[0] ] = false;
											if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache ) == 0 )
											{
												tick();
												// coloco la posicion de cache invalida
												procesador[ procesadorEnQueSeEncuentraCompartido[1] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
												pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache );
												// indica al directorio que el bloque ya no esta en este procesador
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[ numeroBloqueDeMemoria%8].procesadores[procesadorEnQueSeEncuentraCompartido[1] ] = false;
											}
											else
											{
												// libera el directorio
												pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );

												// libero mi cache
												pthread_mutex_unlock( &mutex_cache );

												//printf("\t%s: libere mi cache\n",nombre.c_str());

												// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
												PC -= 4;
												break;
											}
										}
										else if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache ) == 0 )
										{
											tick();
											// coloco la posicion de cache invalida
											procesador[ procesadorEnQueSeEncuentraCompartido[1] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
											pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[1] ].mutex_cache );
											// indica al directorio que el bloque ya no esta en este procesador
											procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[1] ] = false;
											if( pthread_mutex_trylock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache ) == 0 )
											{
												tick();
												// coloco la posicion de cache invalida
												procesador[ procesadorEnQueSeEncuentraCompartido[0] ].cache[ numeroBloqueDeMemoria%4 ].estado = 'i';
												pthread_mutex_unlock( &procesador[ procesadorEnQueSeEncuentraCompartido[0] ].mutex_cache );
												// indica al directorio que el bloque ya no esta en este procesador
												procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[ procesadorEnQueSeEncuentraCompartido[0] ] = false;
											}
											else
											{
												// libera el directorio
												pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );

												// libero mi cache
												pthread_mutex_unlock( &mutex_cache );

												//printf("\t%s: libere mi cache\n",nombre.c_str());

												// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
												PC -= 4;
												break;
											}
										}
										else
										{
											// libera el directorio
											pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );

											// libero mi cache
											pthread_mutex_unlock( &mutex_cache );

											//printf("\t%s: libere mi cache\n",nombre.c_str());

											// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
											PC -= 4;
											break;
										}

									}

									// ahora va a estar modificado
									procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].estado = 'm';

									// indica que este procesador va a tener cargado el bloque de memoria
									procesador[procesadorAlQuePerteneceDireccionMemoria].directorio[numeroBloqueDeMemoria%8].procesadores[numeroProcesador] = true;
									// libera el directorio
									pthread_mutex_unlock( &(procesador[procesadorAlQuePerteneceDireccionMemoria].mutex_directorio) );
									tick();
									cache[ numeroBloqueDeMemoria%4 ].estado = 'm';

								}
								else{
									// libero mi cache
									pthread_mutex_unlock( &mutex_cache );

									//printf("\t%s: libere mi cache\n",nombre.c_str());

									// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
									PC -= 4;
									break;
								}
							}


							// copia en la ccache lo que se encuentra en el registro solicitado
							cache[numeroBloqueDeMemoria%4].palabra[(posicionDeMemoria>>2)%4]=Reg[instruccionActual->p2];

							// libero mi cache
							pthread_mutex_unlock(&mutex_cache);
							//printf("\t%s: libere mi cache\n",nombre.c_str());

						}
						else{
							// no obtuve mi cache no puedo hacer nada :(
							//printf("\t%s: NO obtuve mi cache\n",nombre.c_str());

							// devuelve el PC para que en el siguiente ciclo lo vuelva a intentar desde el inicio
							PC -= 4;
						}

					break;
					default: // instruccion no reconocida
						std::cout << "AHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH :(" << std::endl;
				} // fin switch
				// un ciclo de reloj al terminar caualquier instruccion
				//printf("\t%s: estaba en T\n",nombre.c_str());
				//printf("\t%s: TICK DESPUES DEL switch. Ciclo: %i\n",nombre.c_str(),ciclos);
			} // fin if
			else if (estado == 'e')
			{
				// el procesador esta en espera de que se le asigne un hilo
				// (o ya no quedan hilos pero los otros procesadores aun siguen procesando alguno y la senal se debe seguir enviando)
				//printf("\t%s: estaba en E\n",nombre.c_str());

			}
			else if (estado == 'a')
			{
				//printf("\t%s: Saliendo\n",nombre.c_str());
				corriendo = false;
			}
			//printf("\t%s:esperando\n",nombre.c_str());
			pthread_barrier_wait(&barrera);


		} //fin while
		//printf("\t%s:ADIOS\n",nombre.c_str());


	}

};

// vector que contendra el nombre de los archivos que se van a utilizar
std::vector<std::string> archivos;


int main(){

	// guardara la eleccion del usuario
	char eleccion;
	do{
		// pregunta si desea ingresar un directorio o dar cada archivo con su ruta completa
		std::cout << " Desea ingresar un (D)irectorio o brindar (A)rchivos con ruta completa? [ D | A ] " << std::endl;
		std::cin >> eleccion;
		std::cin.ignore(10000,'\n');
	}
	while(eleccion != 'd' && eleccion != 'D' && eleccion != 'a' && eleccion != 'A');

	std::string archivo;
	std::string ruta;

	if( eleccion == 'a' | eleccion == 'A'){	// se selecciona archivos con ruta completa, solo se solicitan nombres
		std::cout << "para terminar la seleccion no escribir nada" << std::endl;
		do{
			std::cout << "ingrese un archivo con su ruta completa\n(No es necesario si esta en la misma carpeta del ejecutable)" << std::endl;
			std::getline(std::cin, archivo);
			// los nombres se colocan en un vector
			archivos.push_back(archivo);
		}
		while(archivo.length());
		//saca del vector la linea vacia
		archivos.pop_back();

	}
	else{ // se va a insertar un directorio
		std::cout << " ingrese el directorio donde se encuentras los archivos" << std::endl;
		// solicita una ruta
		while(std::getline(std::cin, ruta) && ! ruta.length()) ;
		// si el usuario termina la ruta con '/' no hace nada, de lo contrario se lo agrega al final
		if(ruta[ruta.length()-1] != '/') ruta += '/';
		// solicita el nombre de los archivos que est´an en esa ruta
		std::cout << "para terminar la seleccion no escribir nada" << std::endl;
		do{
			std::cout << " ingrese el nombre del archivo: " << std::endl;
			std::getline(std::cin, archivo);
			archivos.push_back(archivo);
		}
		while(archivo.length());
		//saca del vector la linea vacia
		archivos.pop_back();

		// le agrega la ruta a todos los archivos
		for( std::vector<std::string>::iterator i = archivos.begin(); i != archivos.end(); ++i)
   			*i = ruta+*i;

	}

	int inicioHilo = 0;
	int temporal = 0;
	// recorre todo el vector de archivos
	for( std::vector<std::string>::iterator i = archivos.begin(); i != archivos.end(); ++i){
		//abre el archivo para lectura
        std::ifstream archivoALeer( (*i).c_str() );
        // verifica si puedo abrise
        if( archivoALeer.is_open() )
        {
        	// agrega la posicion donde se encontrara en el vector de instruccion el inicio de este hilo
			inicioHilos.push_back(inicioHilo);
        	while( archivoALeer >> temporal){ // obtiene el numero correspondiente
  				// lo agrega al vector de instrucciones
				instrucciones.push_back(temporal);
				// actualiza la posicion del vector
				++inicioHilo;
        	}
	        archivoALeer.clear();
	        archivoALeer.close();
        }
        else{
            std::cerr << "No se pudo abrir el archivo: " << *i << std::endl;
            archivos.erase(i);
        }
	}
	// la cantidad todal de hilos a correr va a ser equivalente al tamano de posiciones que indican el inicio de los hilos
	cantidadDeHilosPorProcesar = inicioHilos.size();

	// si se lleyo al menos un archivo se sigue con la ejecucion.
	if(cantidadDeHilosPorProcesar) inicioDeEjecucion();

	return 0;
}


/**
  * Funcion que va a correr el hilo
  * recibe el numero de procesador que debe ejecutar
  **/
void* hilo_correrProcesador(void * i){
	long numero_procesador = (long)i;
	procesador[numero_procesador].correr();
}

void inicioDeEjecucion(){
	// crea el procesador 0
 	procesador.push_back( Procesador(0, "Proc0") );
 	procesador.push_back( Procesador(1, "Proc1") );
 	procesador.push_back( Procesador(2, "Proc2") );
	// hilo en el que se ejecutara el procesador1
	pthread_t hiloProcesador1;
	pthread_t hiloProcesador2;
	pthread_t hiloProcesador3;
	// numero hilo que se va a procesar
	// indice de los vectores del nombre y del PC de inicio
	int hiloActual = 0;

	/** inicializa la barrera
	  * &barrera => nombre de la barrera
	  * NULL => atributos de la barrrera, por ser NULL se usa la barrera por defecto
	  * 2 => cantidad de hilos que deben llegar a la barrera para poder continuar
	  *       en este caso son 2, el procesador y el hilo principal
	  **/
	pthread_barrier_init(&barrera, NULL, 4);

	pthread_mutex_init(&mutex_hilos_procesar, NULL);
	/** inicializa la barrera¡
	  * NULL => atributos del mutex, por ser NULL se usa el mutex por defecto
	  **/
	// pone al hilo a correr la funcion de inicio
	pthread_create(&hiloProcesador1, NULL, hilo_correrProcesador, (void*)0);
	pthread_create(&hiloProcesador2, NULL, hilo_correrProcesador, (void*)1);
	pthread_create(&hiloProcesador3, NULL, hilo_correrProcesador, (void*)2);
	int ciclos = 0;
	// mientras exista hilos por procesar va a estar en el while


	int cantidadDeHilosPorAsignar = cantidadDeHilosPorProcesar;
	printf("Cantidad de hilos por asignar: %i\n",cantidadDeHilosPorAsignar);
	while( cantidadDeHilosPorProcesar ){ // continua si quedan hilos por procesar


		

		for(int i = 0; i < 3 && cantidadDeHilosPorAsignar; ++i)
		{
			//printf("CICLO FOR, IT: %i -- Cantidad de hilos por asignar: %i\n",i,cantidadDeHilosPorAsignar);
			if(procesador[i].estado == 'e'){ // verifica si el hilo esta esperando (no tiene hilo asignado)
				//si el hilo esta esperando una asignacion se le asigna uno
				procesador[i].asignar( inicioHilos[hiloActual], archivos[hiloActual], ciclos );
				++hiloActual;
				--cantidadDeHilosPorAsignar;
			}
		}

		

		pthread_barrier_wait(&barrera);  // antes de verificar su estado, para que todos comiencen igual
		//printf("Main thread: First Barrier\n");

				//aumenta el numero de ciclos totales
		++ciclos;
		// aunmente el numero de ciclos de reloj del hilo en proceso.
		++procesador[0].ciclos;
		++procesador[1].ciclos;
		++procesador[2].ciclos;


		pthread_barrier_wait(&barrera); // cuando llegan a esta barrera ya debieron terminar lo correspondiente a ese ciclo
		//printf("Main thread: Second Barrier\"\n");

		pthread_mutex_lock(&mutex_hilos_procesar);
		//printf("Main thread: Obtuve Mutex\"\n");
		
			if( cantidadDeHilosPorProcesar == 0 )
			{		
				//printf("Main thread: Todo 0\"\n");
				// indica que los procesadores ya terminaron
				procesador[0].estado = 'a';
				procesador[1].estado = 'a';
				procesador[2].estado = 'a';	
			}



		pthread_mutex_unlock(&mutex_hilos_procesar);
		



	}
	// finalizan todos los procesadores
	pthread_cancel(hiloProcesador1);
	pthread_cancel(hiloProcesador2);
	pthread_cancel(hiloProcesador3);


	
	// imprime todos los datos de los hilos procesados
	procesador[0].imprimirEstadosDelHistorial();




	// desturye la barrera y el mutex
	pthread_barrier_destroy(&barrera);

	delete[] memoriaPrincipal;
}
