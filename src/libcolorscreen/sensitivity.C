/* Characteristic curves and sensitivity models for film and digital sensors.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <include/sensitivity.h>
namespace colorscreen
{
const struct film_sensitivity::hd_curve_description
      film_sensitivity::hd_curves_properties[]
      = { { "linear-reversal",
            "Linear reversal film",
            { -10, 10, -5, 5, 5, -5, 10, -10 } },
          { "linear-negative",
            "Linear negative film",
            { -10, -10, -5, -5, 5, 5, 10, 10 } },
#if 0
          { "safe-linear-reversal",
            "Linear reversal film",
            { 0, 1, 0, 1, 0.7, 0.3, 3, 0, true } },
          { "safe-linear-negative",
            "Linear negative film",
            { 0, 0, 0, 0, 0.7, 0.7, 3, 1, true } },
          { "spicer-dufay-low",
            "Spicer-Dufay low development",
            { 0.005596021177603383, 0.13326648483876236, 0.9264367078453395,
              0.25357372051981475, 
	      3.8351612385689076, 1.7539186587518052,
              3.8351612385689076, 1.7539186587518052, true } },
          { "spicer-dufay-mid",
            "Spicer-Dufay mid development",
            { 0.005596021177603383, 0.13326648483876236, 0.7346061286699825,
              0.3354524306112632, 2.7042515642547724, 2.207183539226697,
              2.7042515642547724, 2.207183539226697, true } },
          { "spicer-dufay-high",
            "Spicer-Dufay high development",
            { 0.005596021177603383, 0.13326648483876236, 0.664193807155463,
              0.430406706240976, 1.5716733515161239, 2.2480065778918665,
              1.5716733515161239, 2.2480065778918665, true } },
          { "spicer-dufay-reversal-low",
            "Spicer-Dufay reversal low development",
            { 0.10817262955238283, 1.7389218674795455, 0.10817262955238283,
              1.7389218674795455, 3.1461832183539227, 0.19716428686026033,
              3.990309642226858, 0.10466468795122763, true } },
          { "spicer-dufay-reversal-mid",
            "Spicer-Dufay reversal mid development",
            { 1.2834525910476495, 2.253437349590888, 1.2834525910476495,
              2.253437349590888, 3.262801219316541, 0.27367639980747693,
              3.990309642226858, 0.10466468795122763, true } },
          { "spicer-dufay-reversal-high",
            "Spicer-Dufay reversal high development",
            { 2.446334028557678, 2.2031926841007543, 2.446334028557678,
              2.2031926841007543, 3.409735279961495, 0.34393951548211144,
              3.9904123215145204, 0.13004572437028772, true } },
#endif
          { "paget-correction1",
            "Paget correction 1 (to linear)",
            { -2.274010, 3.400111, -1.341965, 1.402846, -0.789100, 0.927726,
              -0.437047, -0.003900 } },
          { "paget-correction2",
            "Paget correction 2 (to linear)",
            { -2.500011, 4.610400, -1.003162, 1.374261, -0.718547, 1.165326,
              -0.255926, -0.118284 } } };
struct hd_curve film_sensitivity::ilfrod_galerie_FB1 (
  (luminosity_t[]){ 1.493581907, 1.576100244, 1.661369193, 1.754889976,
                    1.878667482, 1.994193154, 2.159229829, 2.269254279,
                    2.503056235, 2.640586797, 2.758863081, 2.849633252,
                    2.9349022, 3.006418093, 3.099938875, 3.215464548,
                    3.319987775, 3.50702934 },
  (luminosity_t[]){ 0.008241758, 0.041208791, 0.085164835, 0.151098901,
                    0.263736264, 0.431318681, 0.695054945, 0.865384615,
                    1.263736264, 1.519230769, 1.728021978, 1.876373626,
                    2.010989011, 2.087912088, 2.151098901, 2.195054945,
                    2.217032967, 2.21978022 },
  18
);

struct hd_curve film_sensitivity::kodachrome_25_red (
  (luminosity_t[]){ -1.780479400552193, -1.722698984008314, -1.664909191262774,
                    -1.607110022315574, -1.549295510492928, -1.496721916562256,
                    -1.454650304395721, -1.417829067502056, -1.381003365750458,
                    -1.346806349107341, -1.315237199015420, -1.283665965323129,
                    -1.252091606230285, -1.220516726237348, -1.188937679043674,
                    -1.159984593395142, -1.133668095653636, -1.104714384924994,
                    -1.075761924356573, -1.049439800894070, -1.023116427271346,
                    -0.996794303808843, -0.970470930186119, -0.944146306403173,
                    -0.917821057540117, -0.891494558516839, -0.865166184253229,
                    -0.838838435069730, -0.812510060806120, -0.786182311622621,
                    -0.759853937359011, -0.733526188175512, -0.707198438992013,
                    -0.680870689808514, -0.654544190785236, -0.628217691761958,
                    -0.601893067979012, -0.575569069276177, -0.549247570893785,
                    -0.520295735405475, -0.488716688211801, -0.457138161918218,
                    -0.425563802825374, -0.393990485532715, -0.362418210040239,
                    -0.328217100610684, -0.294017256224209, -0.262448106132288,
                    -0.228250270931884, -0.191425015666078, -0.154602885800826,
                    -0.115152796366506, -0.073075919388325, -0.031004902536182,
                    0.0136921453144496, 0.0662669388938193, 0.1240908269181253,
                    0.1840151545908512, 0.2502444948640363, 0.3033934750366280,
                    0.4552122916893193, 0.5130008870411404, 0.5707923946980227,
                    0.618089297753762 },
  (luminosity_t[]){
      3.7610478917819434,  3.7416988210818674,  3.7144878052891857,
      3.6794148444038983,  3.6314768824579833,  3.575364009173117,
      3.5197937176012424,  3.463978528571703,   3.4044195561647324,
      3.3476707653412503,  3.2930457958153947,  3.236673727380071,
      3.177681010580545,   3.118251519053653,   3.0553278297078243,
      2.9929306140852514,  2.9399700766242205,  2.8770487313288076,
      2.8151756453790746,  2.7574979408624807,  2.698771977000206,
      2.6410942724836115,  2.5823683086213363,  2.5225940854133806,
      2.462295732532584,   2.400949120306108,   2.3380301190611092,
      2.275635247488952,   2.212716246243954,   2.1503213746717957,
      2.087402373426798,   2.0250075018546405,  1.9626126302824831,
      1.9002177587103253,  1.8388711464838479,  1.7775245342573713,
      1.7177503110494157,  1.6585002175143,     1.6013466426705465,
      1.5399976863936544,  1.477073997047826,   1.4145870824293643,
      1.3555943656298388,  1.297475198285047,   1.2402295803949897,
      1.1800489881421958,  1.1209291345130072,  1.0663041649871512,
      1.0088690138778067,  0.949684419808579,   0.8931204741035534,
      0.8343547116138721,  0.7743698754761095,  0.7192987550212258,
      0.6636429242127715,  0.6065241454951811,  0.55072423845666,
      0.4947795117215783,  0.4353031960118461,  0.38591613262833935,
      0.27450354458907134, 0.24829654342942842, 0.2196475744758697,
      0.18535543606395644 },
  64
);
struct hd_curve film_sensitivity::kodachrome_25_green (
  (luminosity_t[]){
      -1.780105204867728, -1.722313707358250, -1.664514254283727,
      -1.606704856752897, -1.551512271867407, -1.501561425686157,
      -1.456858344076123, -1.417408477884699, -1.388474970638207,
      -1.346387545433157, -1.301669835212199, -1.270098080619816,
      -1.238528409627802, -1.204325067769280, -1.172750708676436,
      -1.141173745083130, -1.109596260589733, -1.078017213396058,
      -1.049066628067969, -1.022745754765688, -0.996423006223075,
      -0.956059916593460, -0.929735709530589, -0.903410460667533,
      -0.877085211804476, -0.849003487830291, -0.830576126166207,
      -0.801618248236827, -0.772660891207538, -0.743705617778619,
      -0.717383494316117, -0.580519703753096, -0.551572139645542,
      -0.525836017338488, -0.493037358901124, -0.457059790131609,
      -0.412118478472342, -0.372667830930780, -0.330590172602461,
      -0.288514076974418, -0.246441106746928, -0.201740673045697,
      -0.154414121529295, -0.104461504287731, -0.051883515854462,
      0.0033179622162387, 0.0633186443715803, 0.1189501112620829,
      0.1767580881562982, 0.234562087267991,  0.2959974630557851,
      0.3465008918848786, 0.3792879406335085, 0.4341974535260000,
      0.4919873139209025, 0.5497761054558747, 0.6023193203127679 },
  (luminosity_t[]){
      3.4472848103588594,  3.418644350003886,   3.3833331483582163,
      3.339683520099177,   3.2888890130790935,  3.2306078458056064,
      3.1698927077384838,  3.1113141344176736,  3.065333408905137,
      2.9965038845381935,  2.9235226562108485,  2.8667138130481575,
      2.8116520687949347,  2.749599584853425,   2.6906068680538997,
      2.629430277617539,   2.5678169124538117,  2.5048932231079832,
      2.444592526176772,   2.387963081005858,   2.3297612468164237,
      2.2392255811996176,  2.179800777773556,   2.11950242489276,
      2.0592040720119633,  1.9936628597024866,  1.9517160775223497,
      1.885300534408,      1.8193217660210181,  1.755090096543504,
      1.69741239202691,    1.406922662651747,   1.349155259139264,
      1.2748374584169277,  1.20527387133644,    1.1487503137319508,
      1.0884013050084151,  1.029167569596554,   0.9685275713677419,
      0.9091978973210302,  0.8524888716385202,  0.7939940051021797,
      0.7348416324243745,  0.6750754310778393,  0.6152777673657317,
      0.5570263245456912,  0.5104364401999284,  0.4585297522896594,
      0.4160713278325301,  0.37694827402074926, 0.34092826759183525,
      0.3273086151504505,  0.2982677560150435,  0.2847070894987249,
      0.25743934971547544, 0.23106784898318722, 0.20042782906233736 },
  57
);
struct hd_curve film_sensitivity::kodachrome_25_blue (
  (luminosity_t[]){
      -1.779987576155984, -1.722197783410444, -1.664399750972536,
      -1.606591489950997, -1.551399444907421, -1.500407284715054,
      -1.449936753875084, -1.293622970586838, -1.262043923393163,
      -1.230464876199489, -1.201516791191843, -1.172566830943865,
      -1.140987783750190, -1.109408736556516, -1.015568584934378,
      -0.985007971835387, -0.958685848372884, -0.932363724910381,
      -0.905470490307431, -0.871702924784432, -0.840117279522922,
      -0.808530592461227, -0.775792021662163, -0.749406660928459,
      -0.717305643429599, -0.685731000209432, -0.654151432115665,
      -0.622575249872498, -0.590999588529423, -0.559019207849275,
      -0.525222048809038, -0.493037358901124, -0.457059790131609,
      -0.412118478472342, -0.372667830930780, -0.330590172602461,
      -0.288514076974418, -0.246441106746928, -0.201740673045697,
      -0.154414121529295, -0.104461504287731, -0.051883515854462,
      0.0033179622162387, 0.0611384407126682, 0.1189501112620829,
      0.1803813234917974, 0.2391707160148097, 0.2947057209827446,
      0.3465086332616343, 0.4047000620424126, 0.4657192355342063,
      0.5235078444159003, 0.5813011278685516, 0.6233456261705426 },
  (luminosity_t[]){
      3.3486531355607148,  3.321442119768033,   3.2873221219242734,
      3.2446254567067623,  3.1942836071314042,  3.143397554287832,
      3.089629257053721,   2.780253513483231,   2.717329824137403,
      2.6544061347915746,  2.5962019565517247,  2.5364253892933535,
      2.4735016999475254,  2.4105780106016965,  2.2401522624637633,
      2.19257897793699,    2.134901273420396,   2.0772235689038014,
      2.0123853763813524,  1.9488409694630748,  1.8803848002372643,
      1.8110550815567201,  1.7542451965938448,  1.6924617575499914,
      1.6321344236822442,  1.572903466122336,   1.5095430020491407,
      1.4490215737038308,  1.388936920085888,   1.3281631456978022,
      1.2680269115223708,  1.20527387133644,    1.1487503137319508,
      1.0884013050084151,  1.029167569596554,   0.9685275713677419,
      0.9091978973210302,  0.8524888716385202,  0.7939940051021797,
      0.7348416324243745,  0.6750754310778393,  0.6152777673657317,
      0.5570263245456912,  0.504085306631755,   0.4585297522896594,
      0.42600088931095836, 0.3973347437174164,  0.35724965923307606,
      0.32081747074065925, 0.2955026827663674,  0.26980025816798037,
      0.24358191221022318, 0.21344393850427723, 0.18060862235171227 },
  54
);

struct hd_curve
    film_sensitivity::fujicolor_crystal_archive_digital_pearl_paper (
      (luminosity_t[]){ 0.944085027726432,
                        1.0854898336414,
                        1.26016635859519,
                        1.38909426987061,
                        1.49722735674677,
                        1.59288354898336,
                        1.67606284658041,
                        1.73844731977819,
                        1.83410351201479,
                        1.96719038817006,
                        2.10027726432532,
                        2.18761552680222,
                        2.25,
                        2.31654343807763,
                        2.38724584103512,
                        2.47042513863216,
                        2.6409426987061,
                        2.76155268022181,
                        2.93207024029575,
                        3.04852125693161 },
      (luminosity_t[]){
          0.103950103950104, 0.103950103950104, 0.116424116424116,
          0.141372141372141, 0.182952182952183, 0.249480249480249,
          0.328482328482329, 0.415800415800416, 0.611226611226611,
          0.981288981288981, 1.43451143451143,  1.74220374220374,
          1.91683991683992,  2.05821205821206,  2.1954261954262,
          2.2952182952183,   2.42827442827443,  2.48232848232848,
          2.51975051975052,  2.53222453222453 },
      20
    );


/* exposure is 5000/65535....5000 linear that is  -inf to 3.69 */
struct hd_curve film_sensitivity::linear_sensitivity (
#if 0
  (luminosity_t[]){-2, 4+2},
  (luminosity_t[]){0.32, 6},
#endif
  (luminosity_t[]){ -1.11, 3.7 }, (luminosity_t[]){ -1.11, 3.7 }, 2
);
#if 0
struct synthetic_hd_curve_parameters input_curve = {-2.3, 0.3,
						    -1.0, 0.32,
						    3.8, 4,
						    3.8, 4};
#endif
struct hd_curve_parameters input_curve_params (-3, 0.3, -2.2, 0.35, 3, 4, 3,
                                               4);

struct hd_curve_parameters safe_output_curve_params (0, 0, 0, 0, 0.7, 0.7, 3,
                                                     1);
struct hd_curve_parameters safe_reversal_output_curve_params (0, 1, 0, 1, 0.7,
                                                              0.3, 3, 0);

/* Convert 4-point HD curve parameters P to Richards curve parameters.  */
struct richards_curve_parameters
hd_to_richards_curve_parameters (const hd_curve_parameters &p)
{
  bool is_inverse = p.is_inverted_p ();

  luminosity_t eps = 1e-4;
  luminosity_t v = 1.0;

  luminosity_t z1 = is_inverse ? p.linear1y : p.linear1x;
  luminosity_t z2 = is_inverse ? p.linear2y : p.linear2x;
  luminosity_t min_z = is_inverse ? p.miny : p.minx;
  luminosity_t max_z = is_inverse ? p.maxy : p.maxx;

  luminosity_t d_low = std::abs (z1 - min_z);
  luminosity_t d_high = std::abs (max_z - z2);

  if (d_high > eps)
    v = d_low / d_high;

  if (v < 0.01)
    v = 0.01;
  if (v > 10.0)
    v = 10.0;

  luminosity_t A, K, B, M;

  auto fit = [&] (luminosity_t in1, luminosity_t in2, luminosity_t out1,
                  luminosity_t out2, luminosity_t a, luminosity_t k)
    {
      luminosity_t min_out = std::min (a, k) + eps;
      luminosity_t max_out = std::max (a, k) - eps;
      out1 = std::clamp (out1, min_out, max_out);
      out2 = std::clamp (out2, min_out, max_out);

      luminosity_t vv1 = std::pow ((k - a) / (out1 - a), v) - 1.0;
      luminosity_t vv2 = std::pow ((k - a) / (out2 - a), v) - 1.0;

      /* Clamp to avoid log of zero or extreme values.  */
      vv1 = std::clamp (vv1, eps, (luminosity_t)1e15);
      vv2 = std::clamp (vv2, eps, (luminosity_t)1e15);

      luminosity_t L1 = std::log (vv1);
      luminosity_t L2 = std::log (vv2);

      luminosity_t denom = in1 - in2;
      if (std::abs (denom) < eps)
        denom = (denom >= 0 ? eps : -eps);

      B = (L2 - L1) / denom;
      /* Clamp B and M to reasonable ranges to avoid numerical instability.  */
      B = std::clamp (B, (luminosity_t)-100.0, (luminosity_t)100.0);
      if (std::abs (B) < eps)
        B = (B >= 0 ? eps : -eps);

      M = in1 + L1 / B;
      M = std::clamp (M, (luminosity_t)-100.0, (luminosity_t)100.0);
    };

  if (!is_inverse)
    {
      A = p.miny;
      K = p.maxy;
      fit (p.linear1x, p.linear2x, p.linear1y, p.linear2y, A, K);
    }
  else
    {
      A = p.minx;
      K = p.maxx;
      fit (p.linear1y, p.linear2y, p.linear1x, p.linear2x, A, K);
    }
  return richards_curve_parameters (A, K, B, M, v, is_inverse);
}

/* Helper function to generate hd_curve_parameters perfectly representing a
   Richard's curve RP.  */
struct hd_curve_parameters
richards_to_hd_curve_parameters (const richards_curve_parameters &rp)
{
  luminosity_t A = rp.A, K = rp.K, B = rp.B, M = rp.M, v = rp.v;
  bool inverse = rp.is_inverse;
  luminosity_t eps = 1e-4;

  auto pick = [&] (luminosity_t a, luminosity_t k)
    {
      luminosity_t delta_o = 0.1 * std::abs (k - a);
      if (delta_o == 0)
        delta_o = 1.0;

      luminosity_t o1 = a + (k > a ? delta_o : -delta_o);
      luminosity_t o2 = k - (k > a ? delta_o : -delta_o);

      auto solve = [&] (luminosity_t out)
        {
          luminosity_t vv = std::max (
              eps, (luminosity_t)(std::pow ((k - a) / (out - a), v) - 1.0));
          return M - std::log (vv) / B;
        };

      auto formula = [&] (luminosity_t in)
        {
          return a
                 + (k - a)
                       / std::pow (1.0 + std::exp (-B * (in - M)), 1.0 / v);
        };

      luminosity_t i1 = solve (o1);
      luminosity_t i2 = solve (o2);

      // D is the characteristic interval on the independent axis.
      luminosity_t D = std::abs (i1 - i2);
      if (D == 0)
        D = 1.0;
      D *= 10.0; // Standardized buffer

      luminosity_t min_in = std::min (i1, i2) - v * D;
      luminosity_t max_in = std::max (i1, i2) + D;

      // Calculate exact boundary locations on the 'output' axis for H&D
      // endpoints
      luminosity_t b1 = formula (min_in);
      luminosity_t b2 = formula (max_in);

      return std::make_tuple (min_in, max_in, i1, o1, i2, o2, b1, b2);
    };

  if (!inverse)
    {
      auto [minx, maxx, x1, y1, x2, y2, y_min, y_max] = pick (A, K);
      // Ensure X-axis monotonicity
      if (x1 > x2)
        {
          std::swap (x1, x2);
          std::swap (y1, y2);
        }
      // In Direct Mode, the curve must reach fog and saturation limits (A, K)
      luminosity_t r_miny = (y1 < y2) ? std::min (A, K) : std::max (A, K);
      luminosity_t r_maxy = (y1 < y2) ? std::max (A, K) : std::min (A, K);
      return hd_curve_parameters (minx, r_miny, x1, y1, x2, y2, maxx, r_maxy);
    }
  else
    {
      auto [miny, maxy, y1, x1, y2, x2, x_min, x_max] = pick (A, K);
      // Ensure X-axis (Output) monotonicity for the renderer
      luminosity_t cx1 = x1, cy1 = y1, cx2 = x2, cy2 = y2;
      if (cx1 > cx2)
        {
          std::swap (cx1, cx2);
          std::swap (cy1, cy2);
        }
      // In Inverse Mode, exposure X is bounded by the solved range (x_min,
      // x_max)
      luminosity_t r_minx = std::min (x_min, x_max);
      luminosity_t r_maxx = std::max (x_min, x_max);
      // Pair with density boundaries
      luminosity_t r_miny = (x1 < x2) ? miny : maxy;
      luminosity_t r_maxy = (x1 < x2) ? maxy : miny;
      return hd_curve_parameters (r_minx, r_miny, cx1, cy1, cx2, cy2, r_maxx,
                                  r_maxy);
    }
}

/* Compute cubic bezier curve passing through (X1, Y1), (X3, Y3)
   with control point (X2, Y2) determining derivatives at the endpoints.
   Return coordinates in RX and RY for given parameter T.  */
inline void
bezier (luminosity_t *rx, luminosity_t *ry, luminosity_t x1, luminosity_t y1,
        luminosity_t x2, luminosity_t y2, luminosity_t x3, luminosity_t y3,
        luminosity_t t)
{
  luminosity_t xa = interpolate (x1, x2, t);
  luminosity_t ya = interpolate (y1, y2, t);
  luminosity_t xb = interpolate (x2, x3, t);
  luminosity_t yb = interpolate (y2, y3, t);

  *rx = interpolate (xa, xb, t);
  *ry = interpolate (ya, yb, t);
}

synthetic_hd_curve::synthetic_hd_curve (int points,
                                        struct hd_curve_parameters p)
{
  if (!(p.minx < p.maxx))
    p.maxx = p.minx + 1;
  bool dostart
      = p.minx < p.linear1x && p.linear1x < p.linear2x && p.linear2x <= p.maxx;
  bool doend
      = p.minx <= p.linear1x && p.linear1x < p.linear2x && p.linear2x < p.maxx;
  int n1 = dostart ? points : 1;
  n = n1 + (doend ? points : 1);
  m_owns_memory = true;
  xs = new luminosity_t[n];
  ys = new luminosity_t[n];
  luminosity_t slope
      = p.linear2y != p.linear1y
            ? (p.linear2x - p.linear1x) / (p.linear2y - p.linear1y)
            : 0;
  xs[0] = p.minx;
  ys[0] = p.miny;
  xs[n - 1] = p.maxx;
  ys[n - 1] = p.maxy;

  luminosity_t start_middlex = p.linear1x - (p.linear1y - p.miny) * slope;
  luminosity_t start_middley = p.miny;

  if (start_middlex < p.minx && slope != 0)
    {
      start_middlex = p.minx;
      start_middley = p.linear1y - (p.linear1x - p.minx) / slope;
    }

  luminosity_t end_middlex = p.linear2x + (p.maxy - p.linear2y) * slope;
  luminosity_t end_middley = p.maxy;

  if (end_middlex > p.maxx && slope != 0)
    {
      end_middlex = p.maxx;
      end_middley = p.linear2y + (p.maxx - p.linear2x) / slope;
    }

  for (int i = 0; i < points; i++)
    {
      if (dostart)
        bezier (&xs[i], &ys[i], p.minx, p.miny, start_middlex, start_middley,
                p.linear1x, p.linear1y, i / (luminosity_t)(points - 1));
      if (doend)
        bezier (&xs[i + n1], &ys[i + n1], p.linear2x, p.linear2y, end_middlex,
                end_middley, p.maxx, p.maxy, i / (luminosity_t)(points - 1));
    }
}
/* Evaluate Richards curve with parameters P at position XS.
   If CLAMP is true, ensure output is within [CLAMPMIN, CLAMPMAX].  */
luminosity_t
richards_hd_curve::eval_richards (const richards_curve_parameters &p,
                                  luminosity_t xs, bool clamp,
                                  luminosity_t clampmin, luminosity_t clampmax)
{
  if (!p.is_inverse)
    {
      // Direct: Density = Richards(LogE)
      return p.A
             + (p.K - p.A)
                   / std::pow (1.0 + std::exp (-p.B * (xs - p.M)), 1.0 / p.v);
    }
  else
    {
      // Inverse: Density = Richards^-1(LogE).
      // Valid only between exposure asymptotes A and K.
      //
      luminosity_t eps_x = 1e-8 * std::abs (p.K - p.A);

      // Strictly clamp to open interval (A, K) to avoid log(0)
      if (p.K > p.A)
        {
          if (xs <= p.A + eps_x)
            {
              xs = p.A + eps_x;
              if (clamp)
                return clampmin;
            }
          if (xs >= p.K - eps_x)
            {
              xs = p.K - eps_x;
              if (clamp)
                return clampmax;
            }
        }
      else
        {
          if (xs >= p.A - eps_x)
            {
              xs = p.A - eps_x;
              if (clamp)
                return clampmax;
            }
          if (xs <= p.K + eps_x)
            {
              xs = p.K + eps_x;
              if (clamp)
                return clampmin;
            }
        }

      luminosity_t base = (p.K - p.A) / (xs - p.A);
      luminosity_t vv = std::pow (std::abs (base), p.v) - 1.0;
      if (vv <= 1e-15)
        vv = 1e-15;
      luminosity_t ret = p.M - std::log (vv) / p.B;
      if (clamp)
        ret = std::clamp (ret, std::min (clampmin, clampmax),
                          std::max (clampmin, clampmax));
      return ret;
    }
}
/* Initialize Richards curve by sampling POINTS from HD_CURVE_PARAMETERS P.  */
richards_hd_curve::richards_hd_curve (int points,
                                      const struct hd_curve_parameters &p)
    : hd_curve (nullptr, nullptr, points, true)
{
  xs = new luminosity_t[n];
  ys = new luminosity_t[n];

  richards_curve_parameters rp = hd_to_richards_curve_parameters (p);
  sample (rp, p.minx, p.maxx, rp.is_inverse, p.miny, p.maxy);
}
/* Initialize Richards curve by sampling POINTS from
   RICHARDS_CURVE_PARAMETERS RP.  */
richards_hd_curve::richards_hd_curve (
    int points, const struct richards_curve_parameters &rp)
    : hd_curve (nullptr, nullptr, points, true)
{
  xs = new luminosity_t[n];
  ys = new luminosity_t[n];

  sample (rp, std::min (rp.A, rp.K), std::max (rp.A, rp.K));
}
/* Fill the lookup table by sampling the Richards curve defined by parameters P
   over the range [MIN_X, MAX_X]. If CLAMP is true, ensure output stays
   within [CLAMPMIN, CLAMPMAX].  */
void
richards_hd_curve::sample (const richards_curve_parameters &p,
                           luminosity_t min_x, luminosity_t max_x, bool clamp,
                           luminosity_t clampmin, luminosity_t clampmax)
{
  // Always sample Exposure (X) axis uniformly for optimal resolution in the
  // table.
  for (int i = 0; i < n; i++)
    {
      xs[i] = min_x + i * (max_x - min_x) / (luminosity_t)(n - 1);
      ys[i] = eval_richards (p, xs[i], clamp, clampmin, clampmax);
    }
}
void
hd_curve_parameters::adjust_v (double old_v, double new_v, double B, double M)
{
  auto f = [&] (double in)
    {
      double Z = B * (in - M);
      // Height S = (1 + exp(-Z))^(-1/v) remains constant
      double inner = std::expm1 ((new_v / old_v) * std::log1p (std::exp (-Z)));
      if (inner <= 1e-20)
        inner = 1e-20;
      return M - std::log (inner) / B;
    };

  // 1. Move knots using precise formula
  double old_l1, old_l2, new_l1, new_l2, old_min, old_max;
  if (is_inverted_p ())
    {
      old_l1 = linear1y;
      old_l2 = linear2y;
      old_min = miny;
      old_max = maxy;
      linear1y = f (linear1y);
      linear2y = f (linear2y);
      new_l1 = linear1y;
      new_l2 = linear2y;
    }
  else
    {
      old_l1 = linear1x;
      old_l2 = linear2x;
      old_min = minx;
      old_max = maxx;
      linear1x = f (linear1x);
      linear2x = f (linear2x);
      new_l1 = linear1x;
      new_l2 = linear2x;
    }

  // 2. Adjust endpoints to satisfy solver's heuristic: v = d_low / d_high
  // d_low = |min - l_toe|, d_high = |max - l_shoulder|
  double old_d_toe = std::abs (old_l1 - old_min);
  double old_d_shoulder = std::abs (old_max - old_l2);
  double old_knot_span = std::abs (old_l2 - old_l1);
  double new_knot_span = std::abs (new_l2 - new_l1);

  // Scale distances such that ratio changes by new_v / old_v
  double scale
      = (old_knot_span > 1e-8) ? (new_knot_span / old_knot_span) : 1.0;
  // d_low_new = d_low_old * scale * (v_new / v_old)
  // d_high_new = d_high_old * scale
  double new_d_toe = old_d_toe * scale * (new_v / old_v);
  double new_d_shoulder = old_d_shoulder * scale;

  if (is_inverted_p ())
    {
      miny = (linear1y < linear2y) ? (linear1y - new_d_toe)
                                   : (linear1y + new_d_toe);
      maxy = (linear2y > linear1y) ? (linear2y + new_d_shoulder)
                                   : (linear2y - new_d_shoulder);
    }
  else
    {
      minx = (linear1x < linear2x) ? (linear1x - new_d_toe)
                                   : (linear1x + new_d_toe);
      maxx = (linear2x > linear1x) ? (linear2x + new_d_shoulder)
                                   : (linear2x - new_d_shoulder);
    }
}

void
hd_curve_parameters::adjust_richards (const richards_curve_parameters &old_rp,
                                      const richards_curve_parameters &new_rp)
{
  if (old_rp.is_inverse != new_rp.is_inverse)
    return; // Should not happen in incremental GUI use
  if (old_rp.A != new_rp.A)
    adjust_A (old_rp.A, new_rp.A, old_rp.K);
  if (old_rp.K != new_rp.K)
    adjust_K (old_rp.K, new_rp.K, new_rp.A);
  if (old_rp.M != new_rp.M)
    adjust_M (old_rp.M, new_rp.M);
  if (old_rp.B != new_rp.B)
    adjust_B (old_rp.B, new_rp.B, new_rp.M);
  if (old_rp.v != new_rp.v)
    adjust_v (old_rp.v, new_rp.v, new_rp.B, new_rp.M);
}
}
