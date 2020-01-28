
#include "../../../curves/grumpkin/grumpkin.hpp"
#include "./group_utils.hpp"

namespace plonk
{
namespace stdlib
{
namespace group_utils
{
namespace
{
    static constexpr size_t num_generators = 128;
    static constexpr size_t bit_length = 256;
    static constexpr size_t quad_length = bit_length / 2;
    static std::array<grumpkin::g1::affine_element, num_generators> generators;
    static std::array<std::array<fixed_base_ladder, quad_length>, num_generators> ladders;
    static std::array<std::array<fixed_base_ladder, quad_length>, num_generators> hash_ladders;

    const auto init = []() {
        generators = grumpkin::g1::derive_generators<num_generators>();
        constexpr size_t first_generator_segment = 126;
        constexpr size_t second_generator_segment = 2;
        for (size_t i = 0; i < num_generators; ++i)
        {
            compute_fixed_base_ladder(generators[i], &ladders[i][0]);
        }
        for (size_t i = 0; i < num_generators / 2; ++i)
        {
            for (size_t j = 0; j < first_generator_segment; ++j)
            {
                hash_ladders[i][j] = ladders[i * 2][j + (quad_length - first_generator_segment)];
            }

            for (size_t j = 0; j < second_generator_segment; ++j)
            {
                hash_ladders[i][j + first_generator_segment] = ladders[i * 2 + 1][j + (quad_length - second_generator_segment)];
            }
        }
        return 1;
    }();
}

void compute_fixed_base_ladder(const grumpkin::g1::affine_element& generator, fixed_base_ladder* ladder)
{
    grumpkin::g1::element* ladder_temp =
        static_cast<grumpkin::g1::element*>(aligned_alloc(64, sizeof(grumpkin::g1::element) * (quad_length * 2)));

    grumpkin::g1::element accumulator;
    grumpkin::g1::affine_to_jacobian(generator, accumulator);
    for (size_t i = 0; i < quad_length; ++i) {
        ladder_temp[i] = accumulator;
        grumpkin::g1::dbl(accumulator, accumulator);
        grumpkin::g1::add(accumulator, ladder_temp[i], ladder_temp[quad_length + i]);
        grumpkin::g1::dbl(accumulator, accumulator);
    }
    grumpkin::g1::batch_normalize(&ladder_temp[0], quad_length * 2);
    for (size_t i = 0; i < quad_length; ++i) {
        grumpkin::fq::__copy(ladder_temp[i].x, ladder[quad_length - 1 - i].one.x);
        grumpkin::fq::__copy(ladder_temp[i].y, ladder[quad_length - 1 - i].one.y);
        grumpkin::fq::__copy(ladder_temp[quad_length + i].x, ladder[quad_length - 1 - i].three.x);
        grumpkin::fq::__copy(ladder_temp[quad_length + i].y, ladder[quad_length - 1 - i].three.y);
    }

    grumpkin::fq::field_t eight_inverse = grumpkin::fq::invert(grumpkin::fq::to_montgomery_form({ { 8, 0, 0, 0 } }));
    std::array<grumpkin::fq::field_t, quad_length> y_denominators;
    for (size_t i = 0; i < quad_length; ++i) {

        grumpkin::fq::field_t x_beta = ladder[i].one.x;
        grumpkin::fq::field_t x_gamma = ladder[i].three.x;

        grumpkin::fq::field_t y_beta = ladder[i].one.y;
        grumpkin::fq::field_t y_gamma = ladder[i].three.y;
        grumpkin::fq::field_t x_beta_times_nine = grumpkin::fq::add(x_beta, x_beta);
        x_beta_times_nine = grumpkin::fq::add(x_beta_times_nine, x_beta_times_nine);
        x_beta_times_nine = grumpkin::fq::add(x_beta_times_nine, x_beta_times_nine);
        x_beta_times_nine = grumpkin::fq::add(x_beta_times_nine, x_beta);

        grumpkin::fq::field_t x_alpha_1 = grumpkin::fq::mul(grumpkin::fq::sub(x_gamma, x_beta), eight_inverse);
        grumpkin::fq::field_t x_alpha_2 = grumpkin::fq::mul(grumpkin::fq::sub(x_beta_times_nine, x_gamma), eight_inverse);

        grumpkin::fq::field_t T0 = grumpkin::fq::sub(x_beta, x_gamma);
        y_denominators[i] = (grumpkin::fq::add(grumpkin::fq::add(T0, T0), T0));

        grumpkin::fq::field_t y_alpha_1 = grumpkin::fq::sub(grumpkin::fq::add(grumpkin::fq::add(y_beta, y_beta), y_beta), y_gamma);
        grumpkin::fq::field_t T1 = grumpkin::fq::mul(x_gamma, y_beta);
        T1 = grumpkin::fq::add(grumpkin::fq::add(T1, T1), T1);
        grumpkin::fq::field_t y_alpha_2 = grumpkin::fq::sub(grumpkin::fq::mul(x_beta, y_gamma), T1);

        ladder[i].q_x_1 = x_alpha_1;
        ladder[i].q_x_2 = x_alpha_2;
        ladder[i].q_y_1 = y_alpha_1;
        ladder[i].q_y_2 = y_alpha_2;
    }
    grumpkin::fq::batch_invert(&y_denominators[0], quad_length);
    for (size_t i = 0; i < quad_length; ++i)
    {
        grumpkin::fq::__mul(ladder[i].q_y_1, y_denominators[i], ladder[i].q_y_1);
        grumpkin::fq::__mul(ladder[i].q_y_2, y_denominators[i], ladder[i].q_y_2);
    }
    free(ladder_temp);
}

const fixed_base_ladder* get_ladder(const size_t generator_index, const size_t num_bits)
{
    // find n, such that 2n + 1 >= num_bits
    size_t n;
    if (num_bits == 0)
    {
        n = 0;
    }
    else
    {
        n = (num_bits - 1) >> 1;
        if (((n << 1) + 1)< num_bits)
        {
            ++n;
        }
    }
    const fixed_base_ladder* result = &ladders[generator_index][quad_length - n - 1];
    return result;
}

const fixed_base_ladder* get_hash_ladder(const size_t generator_index, const size_t num_bits)
{
    // find n, such that 2n + 1 >= num_bits
    size_t n;
    if (num_bits == 0)
    {
        n = 0;
    }
    else
    {
        n = (num_bits - 1) >> 1;
        if (((n << 1) + 1)< num_bits)
        {
            ++n;
        }
    }
    const fixed_base_ladder* result = &hash_ladders[generator_index][quad_length - n - 1];
    return result;
}

grumpkin::g1::affine_element get_generator(const size_t generator_index)
{
    return generators[generator_index];
}

grumpkin::fq::field_t compress_native(const grumpkin::fq::field_t& left, const grumpkin::fq::field_t& right)
{
    bool left_skew = false;
    bool right_skew = false;

    uint64_t left_wnafs[255] = { 0 };
    uint64_t right_wnafs[255] = { 0 };

    grumpkin::fq::field_t converted_left = grumpkin::fq::from_montgomery_form(left);
    grumpkin::fq::field_t converted_right = grumpkin::fq::from_montgomery_form(right);

    uint64_t* left_scalar = &(converted_left.data[0]);
    uint64_t* right_scalar = &(converted_right.data[0]);

    barretenberg::wnaf::fixed_wnaf<255, 1, 2>(left_scalar, &left_wnafs[0], left_skew, 0);
    barretenberg::wnaf::fixed_wnaf<255, 1, 2>(right_scalar, &right_wnafs[0], right_skew, 0);

    const auto compute_split_scalar = [](uint64_t* wnafs, const size_t range) {
        grumpkin::fr::field_t result = grumpkin::fr::zero;
        grumpkin::fr::field_t three = grumpkin::fr::to_montgomery_form({ { 3, 0, 0, 0 } });
        for (size_t i = 0; i < range; ++i) {
            uint64_t entry = wnafs[i];
            grumpkin::fr::field_t prev = grumpkin::fr::add(result, result);
            prev = grumpkin::fr::add(prev, prev);
            if ((entry & 0xffffff) == 0) {
                if (((entry >> 31UL) & 1UL) == 1UL) {
                    result = grumpkin::fr::sub(prev, grumpkin::fr::one);
                } else {
                    result = grumpkin::fr::add(prev, grumpkin::fr::one);
                }
            } else {
                if (((entry >> 31UL) & 1UL) == 1UL) {
                    result = grumpkin::fr::sub(prev, three);
                } else {
                    result = grumpkin::fr::add(prev, three);
                }
            }
        }
        return result;
    };

    grumpkin::fr::field_t grumpkin_scalars[4]{ compute_split_scalar(&left_wnafs[0], 126),
                                        compute_split_scalar(&left_wnafs[126], 2),
                                        compute_split_scalar(&right_wnafs[0], 126),
                                        compute_split_scalar(&right_wnafs[126], 2) };
    if (left_skew)
    {
        grumpkin::fr::__add(grumpkin_scalars[1], grumpkin::fr::one, grumpkin_scalars[1]);
    }
    if (right_skew)
    {
        grumpkin::fr::__add(grumpkin_scalars[3], grumpkin::fr::one, grumpkin_scalars[3]);
    }

    grumpkin::g1::affine_element grumpkin_points[4]{
        plonk::stdlib::group_utils::get_generator(0),
        plonk::stdlib::group_utils::get_generator(1),
        plonk::stdlib::group_utils::get_generator(2),
        plonk::stdlib::group_utils::get_generator(3),
    };

    grumpkin::g1::element result_points[4]{
        grumpkin::g1::group_exponentiation_inner(grumpkin_points[0], grumpkin_scalars[0]),
        grumpkin::g1::group_exponentiation_inner(grumpkin_points[1], grumpkin_scalars[1]),
        grumpkin::g1::group_exponentiation_inner(grumpkin_points[2], grumpkin_scalars[2]),
        grumpkin::g1::group_exponentiation_inner(grumpkin_points[3], grumpkin_scalars[3]),
    };

    grumpkin::g1::element hash_output_left;
    grumpkin::g1::element hash_output_right;

    grumpkin::g1::add(result_points[0], result_points[1], hash_output_left);
    grumpkin::g1::add(result_points[2], result_points[3], hash_output_right);

    grumpkin::g1::element hash_output;
    grumpkin::g1::add(hash_output_left, hash_output_right, hash_output);
    hash_output = grumpkin::g1::normalize(hash_output);
    return hash_output.x;
}
}
}
}