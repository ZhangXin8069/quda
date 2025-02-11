// QUDA headers
#include <quda.h>
#include <color_spinor_field.h>
#include <blas_quda.h>
#include <instantiate.h>

// External headers
#include <test.h>
#include <misc.h>
#include <dslash_reference.h>

using test_t = ::testing::tuple<QudaPrecision, QudaSiteSubset, QudaDilutionType, int>;

class DilutionTest : public ::testing::TestWithParam<test_t>
{
protected:
  QudaPrecision precision;
  QudaSiteSubset site_subset;
  QudaDilutionType dilution_type;
  int nSpin;

public:
  DilutionTest() :
    precision(::testing::get<0>(GetParam())),
    site_subset(::testing::get<1>(GetParam())),
    dilution_type(testing::get<2>(GetParam())),
    nSpin(testing::get<3>(GetParam()))
  {
  }
};

TEST_P(DilutionTest, verify)
{
  using namespace quda;

  if (!is_enabled_spin(nSpin) || !is_enabled(precision)) GTEST_SKIP();

  // Set some parameters
  QudaGaugeParam gauge_param = newQudaGaugeParam();
  QudaInvertParam inv_param = newQudaInvertParam();
  setWilsonGaugeParam(gauge_param);
  setInvertParam(inv_param);

  ColorSpinorParam param;
  constructWilsonTestSpinorParam(&param, &inv_param, &gauge_param);
  param.siteSubset = site_subset;
  if (site_subset == QUDA_PARITY_SITE_SUBSET) param.x[0] /= 2;
  param.nSpin = nSpin;
  param.setPrecision(precision, precision, true); // change order to native order
  param.location = QUDA_CUDA_FIELD_LOCATION;
  param.create = QUDA_NULL_FIELD_CREATE;
  ColorSpinorField src(param);

  // compute number of blocks when using block dilution
  int block_volume = 1;
  lat_dim_t block_size = {dilution_block_size[0], dilution_block_size[1], dilution_block_size[2], dilution_block_size[3]};
  if (src.SiteSubset() == QUDA_PARITY_SITE_SUBSET) block_size[0] /= 2;
  for (int i = 0; i < src.Ndim(); i++) block_volume *= block_size[i];
  int n_blocks = comm_size() * src.Volume() / block_volume;
  if (dilution_type == QUDA_DILUTION_BLOCK) {
    logQuda(QUDA_VERBOSE, "Dilution block size = %d x %d x %d x %d\n", block_size[0], block_size[1], block_size[2],
            block_size[3]);
    logQuda(QUDA_VERBOSE, "Number of dilution blocks = %d\n", n_blocks);
  }

  RNG rng(src, 1234);

  for (int i = 0; i < Nsrc; i++) {
    spinorNoise(src, rng, QUDA_NOISE_GAUSS); // Populate the host spinor with random numbers.

    size_t size = 0;
    switch (dilution_type) {
    case QUDA_DILUTION_SPIN: size = src.Nspin(); break;
    case QUDA_DILUTION_COLOR: size = src.Ncolor(); break;
    case QUDA_DILUTION_SPIN_COLOR: size = src.Nspin() * src.Ncolor(); break;
    case QUDA_DILUTION_SPIN_COLOR_EVEN_ODD: size = src.Nspin() * src.Ncolor() * src.SiteSubset(); break;
    case QUDA_DILUTION_BLOCK: size = n_blocks; break;
    default: errorQuda("Invalid dilution type %d", dilution_type);
    }

    std::vector<ColorSpinorField> v(size, param);
    spinorDilute(v, src, dilution_type, block_size);

    param.create = QUDA_ZERO_FIELD_CREATE;
    ColorSpinorField sum(param);
    blas::block::axpy(std::vector<double>(v.size(), 1.0), v, sum); // reassemble the vector

    { // check its norm matches the original
      auto src2 = blas::norm2(src);
      auto sum2 = blas::norm2(sum);
      EXPECT_EQ(sum2, src2);
    }

    { // check for component-by-component matching
      auto sum2 = blas::xmyNorm(src, sum);
      EXPECT_EQ(sum2, 0.0);
    }
  }
}

using ::testing::Combine;
using ::testing::get;
using ::testing::Values;

auto test_str = [](testing::TestParamInfo<test_t> param) {
  return std::string(get_prec_str(get<0>(param.param))) + "_" + get_dilution_type_str(get<2>(param.param));
};

auto precisions = Values(QUDA_DOUBLE_PRECISION, QUDA_SINGLE_PRECISION);

INSTANTIATE_TEST_SUITE_P(WilsonFull, DilutionTest,
                         Combine(precisions, Values(QUDA_FULL_SITE_SUBSET),
                                 Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR,
                                        QUDA_DILUTION_SPIN_COLOR_EVEN_ODD, QUDA_DILUTION_BLOCK),
                                 Values(4)),
                         test_str);

INSTANTIATE_TEST_SUITE_P(
  WilsonParity, DilutionTest,
  Combine(precisions, Values(QUDA_PARITY_SITE_SUBSET),
          Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR, QUDA_DILUTION_BLOCK), Values(4)),
  test_str);

INSTANTIATE_TEST_SUITE_P(CoarseFull, DilutionTest,
                         Combine(precisions, Values(QUDA_FULL_SITE_SUBSET),
                                 Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR,
                                        QUDA_DILUTION_SPIN_COLOR_EVEN_ODD),
                                 Values(2)),
                         test_str);

INSTANTIATE_TEST_SUITE_P(CoarseParity, DilutionTest,
                         Combine(precisions, Values(QUDA_PARITY_SITE_SUBSET),
                                 Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR), Values(2)),
                         test_str);

INSTANTIATE_TEST_SUITE_P(StaggeredFull, DilutionTest,
                         Combine(precisions, Values(QUDA_FULL_SITE_SUBSET),
                                 Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR,
                                        QUDA_DILUTION_SPIN_COLOR_EVEN_ODD),
                                 Values(1)),
                         test_str);

INSTANTIATE_TEST_SUITE_P(StaggeredParity, DilutionTest,
                         Combine(precisions, Values(QUDA_PARITY_SITE_SUBSET),
                                 Values(QUDA_DILUTION_SPIN, QUDA_DILUTION_COLOR, QUDA_DILUTION_SPIN_COLOR), Values(1)),
                         test_str);

struct dilution_test : quda_test {
  void display_info() const override
  {
    quda_test::display_info();
    printfQuda("S_dimension T_dimension Ls_dimension\n");
    printfQuda("%3d/%3d/%3d     %3d         %2d\n", xdim, ydim, zdim, tdim, Lsdim);
  }

  dilution_test(int argc, char **argv) : quda_test("Dilution Test", argc, argv) { }
};

int main(int argc, char **argv)
{
  dilution_test test(argc, argv);
  test.init();
  return test.execute();
}
