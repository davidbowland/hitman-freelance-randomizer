// Written by hand from NPC names and repository ids measured in game and the mover table (MoverTable.inc). Rows: {mild
// scene (as fr::SceneOf gives it, any case), repository id, actor name}: a target of the main story mission at that
// location who is an NPC of its Freelancer level, always: never a Freelancer target (StoryTargetExclusions), shuffle on
// or off, map variant or not. None of them is in the game's own target pool (the mild mission bricks'
// Helper_Evergreen_Spawn_Targets m_aActors, IOI's patches only: read for 16 of the 19 locations; New York's, Haven's
// and Ambrose's from the logged pools); the target pool swap could draw them from the mover table, as it did Sophia
// Washington at Sgail. The comment: where the id was read. Not found in any census of the location's Freelancer level,
// so not listed: Santa Fortuna's Andrea Martinez (Paris's "Andrea Martinez" is another NPC), Hokkaido's Erich Soders,
// Berlin's ICA agents. Hawke's Bay has no Freelancer mission.

// Miami: The Finish Line
{"assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_pinochle.entity", "ee454990-0c4b-49e5-9572-a67887325283", "Robert Knox"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_pinochle.entity", "c0ab162c-1502-40d5-801f-c5471289d6b7", "Sierra Knox"}, // census (stationary); SealedActors.inc

// Santa Fortuna: Three-Headed Serpent
{"assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity", "00df867e-f27f-4904-8bc7-9504443ccb5a", "Rico Delgado"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity", "b87b242e-4ef4-42d8-94ed-17cbfc9009bf", "Jorge Franco"}, // MoverTable.inc

// Mumbai: Chasing a Ghost
{"assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity", "78f98c70-b7be-4578-9b6a-1c96a3e1ff1a", "Dawood Rangan"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity", "076f23cc-09d8-423f-b890-74020f53b1d6", "Vanya Shah"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity", "c7c9e213-16f9-4215-bf07-dd8f801ce3e0", "Wazir Kale"}, // MoverTable.inc

// Whittleton Creek: Another Life
{"assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity", "672a7a52-a08a-45cd-a061-ced6a7b8d8c4", "Janus"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity", "b8f0bf6c-4826-4de2-a785-2d139967e09c", "Nolan Cassidy"}, // MoverTable.inc

// Isle of Sgail: The Ark Society
{"assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity", "7eb39f2d-1030-44d2-be82-6df608085ec0", "Zoe Washington"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity", "67f39ab8-c25f-48c3-84be-0ec495a553ec", "Sophia Washington"}, // MoverTable.inc

// New York: Golden Handshake
{"assembly:/_pro/scenes/missions/greedy/mission_raccoon/scene_raccoon_mild_blackjack.entity", "ad93e268-3d6e-4aba-bec0-607cb5451ac7", "Athena Savalas"}, // MoverTable.inc

// Haven Island: The Last Resort
{"assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_mild_hilo.entity", "7504b78e-e766-42fe-930c-c5640f5f507b", "Tyson Williams"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_mild_hilo.entity", "0afcc59e-6d6e-433f-8404-7699df872c9d", "Steven Bradley"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_mild_hilo.entity", "5bc06fb1-bfb3-48ef-94ae-6f18c16c1eee", "Ljudmila Vetrova"}, // MoverTable.inc

// Dubai: On Top of the World
{"assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_mild_chess.entity", "9571d196-8d67-4d94-8dad-6e2d970d7a91", "Marcus Stuyvesant"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_mild_chess.entity", "bd0689d6-07b4-4757-b8ee-cac19f1c9e16", "Carl Ingram"}, // census (stationary)

// Dartmoor: Death in the Family
{"assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity", "080efb03-a66a-401e-b6df-4eac496e9e2d", "Alexa Carlisle"}, // MoverTable.inc

// Chongqing: End of an Era
{"assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity", "967abcf9-2672-4e81-8fef-211aaa366747", "Hush"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity", "a7fd7a4f-2bee-4787-bc60-90f9dd64233b", "Imogen Royce"}, // MoverTable.inc

// Mendoza: The Farewell
{"assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity", "57907f04-329e-4faf-b753-7e95d5c2e085", "Don Archibald Yates"}, // census (mover)
{"assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity", "651ceb9a-117f-4f8d-89dd-9b6bd2a38b5a", "Tamara Vidal"}, // census (stationary)

// Ambrose Island: Shadows in the Water
{"assembly:/_pro/scenes/missions/rocky/scene_rocky_mild_poker.entity", "4f80ca40-a8f3-4a5e-9add-4c74d3bb5bcc", "Noel Crest"}, // MoverTable.inc (a guard)
{"assembly:/_pro/scenes/missions/rocky/scene_rocky_mild_poker.entity", "666521d2-1efe-4d0a-b7f7-2afeb08262b6", "Sinhi \"Akka\" Venthan"}, // census (mover)

// Paris: The Showstopper
{"assembly:/_pro/scenes/missions/paris/scene_peacock_mild_craps.entity", "052434e7-f451-462f-a9d7-13657cb047c0", "Viktor Novikov"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/paris/scene_peacock_mild_craps.entity", "edad702b-5b37-4dc1-a47c-36a1588f1d3f", "Dalia Margolis"}, // MoverTable.inc

// Sapienza: World of Tomorrow
{"assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity", "0dfaea51-3c36-4722-9eff-f1e7ef139878", "Silvio Caruso"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity", "94ab740b-b30f-4086-9aea-5c9c0de28456", "Francesca De Santis"}, // MoverTable.inc

// Marrakesh: A Gilded Cage
{"assembly:/_pro/scenes/missions/marrakesh/scene_spider_mild_mancala.entity", "b38b0b62-8071-4761-b2a5-2f635cd8da1b", "Claus Hugo Strandberg"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/marrakesh/scene_spider_mild_mancala.entity", "ca31c88f-d15e-407b-8407-231f1b068402", "Reza Zaydan"}, // MoverTable.inc

// Bangkok: Club 27
{"assembly:/_pro/scenes/missions/bangkok/scene_tiger_mild_sicbo.entity", "ee3f55b8-12f8-4245-8ef2-3022b4f6f120", "Jordan Cross"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/bangkok/scene_tiger_mild_sicbo.entity", "f65fff84-6cad-4a11-9a0a-b89430c03397", "Ken Morgan"}, // MoverTable.inc

// Colorado: Freedom Fighters
{"assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity", "5b54d9fb-fa85-4302-a8d5-c5c5e97344c4", "Sean Rose"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity", "963c2774-cb9a-4b0c-ab69-210b2405383b", "Penelope Graves"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity", "1a8a827f-932e-49c0-a1b3-e3201795ae19", "Ezra Berg"}, // MoverTable.inc
{"assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity", "d94f3e83-36e3-453c-8d4b-28c93229826a", "Maya Parvati"}, // MoverTable.inc

// Hokkaido: Situs Inversus
{"assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_mild_shogi.entity", "9bebb40a-3746-4ba2-8bfc-a1fcabaec72c", "Yuki Yamazaki"}, // MoverTable.inc
